#include <mulan/core/image/image.h>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <future>

namespace mulan::core {
namespace {

std::shared_ptr<Image> makeOrientationImage() {
    // Top-left red, top-right green, bottom-left blue, bottom-right yellow.
    std::vector<uint8_t> rgba = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
    };
    return Image::createFromBuffer(2, 2, PixelFormat::RGBA8, std::move(rgba), ImageOrigin::TopLeft);
}

TEST(ImageTests, ConvertsOriginWithoutMutatingSource) {
    auto topLeft = makeOrientationImage();
    auto bottomLeft = topLeft->convertedOrigin(ImageOrigin::BottomLeft);

    ASSERT_TRUE(bottomLeft && bottomLeft->valid());
    EXPECT_EQ(topLeft->origin(), ImageOrigin::TopLeft);
    EXPECT_EQ(bottomLeft->origin(), ImageOrigin::BottomLeft);
    EXPECT_EQ(topLeft->scanline(0)[0], 255);     // red
    EXPECT_EQ(bottomLeft->scanline(0)[2], 255);  // blue

    auto roundTrip = bottomLeft->convertedOrigin(ImageOrigin::TopLeft);
    ASSERT_TRUE(roundTrip);
    EXPECT_EQ(std::vector<uint8_t>(roundTrip->data(), roundTrip->data() + roundTrip->totalBytes()),
              std::vector<uint8_t>(topLeft->data(), topLeft->data() + topLeft->totalBytes()));
}

TEST(ImageTests, FileAndMemoryDecodeHaveIdenticalTopLeftOrientation) {
    const auto path = std::filesystem::temp_directory_path() / "mulan_image_orientation_test.png";
    auto source = makeOrientationImage();
    ASSERT_TRUE(source->savePNG(path.string()));

    ImageDecodeOptions options;
    options.channels = ImageChannelLayout::RGBA;
    auto fromFile = Image::load(path.string(), options);

    std::ifstream stream(path, std::ios::binary);
    std::vector<char> encoded((std::istreambuf_iterator<char>(stream)), {});
    stream.close();
    auto fromMemory =
            Image::loadFromMemory({ reinterpret_cast<const std::byte*>(encoded.data()), encoded.size() }, options);
    std::filesystem::remove(path);

    ASSERT_TRUE(fromFile);
    ASSERT_TRUE(fromMemory);
    EXPECT_EQ((*fromFile)->origin(), ImageOrigin::TopLeft);
    EXPECT_EQ((*fromFile)->format(), PixelFormat::RGBA8);
    EXPECT_EQ(std::vector<uint8_t>((*fromFile)->data(), (*fromFile)->data() + (*fromFile)->totalBytes()),
              std::vector<uint8_t>((*fromMemory)->data(), (*fromMemory)->data() + (*fromMemory)->totalBytes()));
    EXPECT_EQ((*fromFile)->scanline(0)[0], 255);  // top-left remains red
}

TEST(ImageTests, RejectsInvalidDataAndConfiguredLimits) {
    const std::array<std::byte, 4> garbage{};
    EXPECT_FALSE(Image::loadFromMemory(garbage));

    const auto path = std::filesystem::temp_directory_path() / "mulan_image_limit_test.png";
    ASSERT_TRUE(makeOrientationImage()->savePNG(path.string()));
    ImageDecodeOptions limits;
    limits.maxWidth = 1;
    EXPECT_FALSE(Image::load(path.string(), limits));
    std::filesystem::remove(path);
}

TEST(ImageTests, ConcurrentDecodeDoesNotShareOrientationState) {
    const auto path = std::filesystem::temp_directory_path() / "mulan_image_concurrent_test.png";
    ASSERT_TRUE(makeOrientationImage()->savePNG(path.string()));

    std::array<std::future<bool>, 8> jobs;
    for (auto& job : jobs) {
        job = std::async(std::launch::async, [path] {
            for (int i = 0; i < 32; ++i) {
                auto decoded = Image::load(path.string());
                if (!decoded || (*decoded)->origin() != ImageOrigin::TopLeft || (*decoded)->scanline(0)[0] != 255)
                    return false;
            }
            return true;
        });
    }
    for (auto& job : jobs)
        EXPECT_TRUE(job.get());
    std::filesystem::remove(path);
}

}  // namespace
}  // namespace mulan::core
