#include <mulan/core/profiling/profile.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

namespace {

using mulan::profiling::ProfileNode;

const ProfileNode* findNode(const std::vector<ProfileNode>& nodes, std::string_view name) {
    for (const ProfileNode& node : nodes) {
        if (node.name == name)
            return &node;
    }
    return nullptr;
}

TEST(Profiling, CapturesOnlyTheManuallySelectedInterval) {
    mulan::profiling::stopCapture();
    { mulan::profiling::ScopedZone ignoredBefore("ignored-before"); }
    mulan::profiling::startCapture();
    { mulan::profiling::ScopedZone included("included"); }
    const auto result = mulan::profiling::stopCapture();
    { mulan::profiling::ScopedZone ignoredAfter("ignored-after"); }

    ASSERT_EQ(result.threads.size(), 1u);
    EXPECT_NE(findNode(result.threads.front().roots, "included"), nullptr);
    EXPECT_EQ(findNode(result.threads.front().roots, "ignored-before"), nullptr);
    EXPECT_EQ(findNode(result.threads.front().roots, "ignored-after"), nullptr);
}

TEST(Profiling, BuildsNestedCallTreeAndExclusiveTime) {
    mulan::profiling::startCapture();
    EXPECT_TRUE(mulan::profiling::isCapturing());
    mulan::profiling::setThreadName("ProfileTest");
    {
        mulan::profiling::ScopedZone outer("outer");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        {
            mulan::profiling::ScopedZone inner("inner");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    const auto result = mulan::profiling::stopCapture();
    EXPECT_FALSE(mulan::profiling::isCapturing());
    ASSERT_EQ(result.threads.size(), 1u);
    EXPECT_EQ(result.threads.front().name, "ProfileTest");
    const ProfileNode* outer = findNode(result.threads.front().roots, "outer");
    ASSERT_NE(outer, nullptr);
    ASSERT_EQ(outer->children.size(), 1u);
    EXPECT_EQ(outer->children.front().name, "inner");
    EXPECT_EQ(outer->callCount, 1u);
    EXPECT_GE(outer->inclusiveNanoseconds, outer->selfNanoseconds);
    EXPECT_GE(outer->inclusiveNanoseconds, outer->children.front().inclusiveNanoseconds);
}

TEST(Profiling, AggregatesRepeatedCallsAndFormatsTree) {
    mulan::profiling::startCapture();
    for (int i = 0; i < 3; ++i) {
        mulan::profiling::ScopedZone zone("repeat");
    }
    mulan::profiling::markFrame();

    const auto result = mulan::profiling::stopCapture();
    ASSERT_EQ(result.threads.size(), 1u);
    const ProfileNode* repeat = findNode(result.threads.front().roots, "repeat");
    ASSERT_NE(repeat, nullptr);
    EXPECT_EQ(repeat->callCount, 3u);
    EXPECT_EQ(result.frameCount, 1u);
    const std::string text = mulan::profiling::formatTree(result);
    EXPECT_NE(text.find("repeat"), std::string::npos);
    EXPECT_NE(text.find("calls=3"), std::string::npos);
    const std::string html = mulan::profiling::formatHtml(result);
    EXPECT_NE(html.find("<title>Mulan Profiler</title>"), std::string::npos);
    EXPECT_NE(html.find("repeat"), std::string::npos);
    EXPECT_NE(html.find("id=\"search\""), std::string::npos);
}

TEST(Profiling, WritesHtmlIntoUniqueSessionDirectory) {
    mulan::profiling::startCapture();
    { mulan::profiling::ScopedZone zone("session-report"); }
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("mulan-profiler-test-" + unique);
    const auto result = mulan::profiling::stopCapture();
    const std::string reportText = mulan::profiling::writeHtmlReportToDirectory(result, root.string());
    const std::filesystem::path report{ reportText };
    ASSERT_FALSE(report.empty());
    EXPECT_EQ(report.filename(), "report.html");
    EXPECT_EQ(report.parent_path().parent_path(), root);
    EXPECT_TRUE(std::filesystem::is_regular_file(report));

    std::error_code ignored;
    std::filesystem::remove(report, ignored);
    std::filesystem::remove(report.parent_path(), ignored);
    std::filesystem::remove(root, ignored);
}

}  // namespace
