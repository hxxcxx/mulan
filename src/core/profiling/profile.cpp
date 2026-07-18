#include "profile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <process.h>
#include <sstream>
#include <thread>

namespace mulan::profiling {
namespace {

using Clock = std::chrono::steady_clock;

struct NodeData {
    std::string name;
    std::uint64_t callCount = 0;
    std::uint64_t inclusiveNanoseconds = 0;
    std::uint64_t selfNanoseconds = 0;
    std::vector<NodeData> children;
};

struct ActiveZone {
    NodeData* node = nullptr;
    Clock::time_point startedAt;
    std::uint64_t childNanoseconds = 0;
};

struct ThreadState {
    std::mutex mutex;
    std::string name;
    std::uint64_t id = 0;
    std::uint64_t epoch = 0;
    std::vector<NodeData> roots;
    std::vector<ActiveZone> stack;
};

struct Registry {
    std::mutex mutex;
    std::vector<std::shared_ptr<ThreadState>> threads;
    std::atomic<bool> capturing{ false };
    std::atomic<std::uint64_t> epoch{ 1 };
    std::atomic<std::uint64_t> frameCount{ 0 };
};

Registry& registry() {
    static Registry value;
    return value;
}

ThreadState& currentThreadState() {
    thread_local std::shared_ptr<ThreadState> state = [] {
        auto value = std::make_shared<ThreadState>();
        value->id = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        value->name = "Thread " + std::to_string(value->id);
        value->epoch = registry().epoch.load(std::memory_order_relaxed);
        std::lock_guard lock(registry().mutex);
        registry().threads.push_back(value);
        return value;
    }();
    return *state;
}

NodeData& findOrAdd(std::vector<NodeData>& nodes, const char* name) {
    const std::string_view requested = name != nullptr ? std::string_view(name) : std::string_view("<unnamed>");
    const auto found = std::find_if(nodes.begin(), nodes.end(),
                                    [requested](const NodeData& node) { return node.name == requested; });
    if (found != nodes.end())
        return *found;
    nodes.push_back(NodeData{ .name = std::string(requested) });
    return nodes.back();
}

ProfileNode copyNode(const NodeData& source) {
    ProfileNode result{
        .name = source.name,
        .callCount = source.callCount,
        .inclusiveNanoseconds = source.inclusiveNanoseconds,
        .selfNanoseconds = source.selfNanoseconds,
    };
    result.children.reserve(source.children.size());
    for (const NodeData& child : source.children)
        result.children.push_back(copyNode(child));
    return result;
}

std::string durationText(std::uint64_t nanoseconds) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3);
    if (nanoseconds >= 1'000'000'000)
        output << static_cast<double>(nanoseconds) / 1'000'000'000.0 << " s";
    else if (nanoseconds >= 1'000'000)
        output << static_cast<double>(nanoseconds) / 1'000'000.0 << " ms";
    else if (nanoseconds >= 1'000)
        output << static_cast<double>(nanoseconds) / 1'000.0 << " us";
    else
        output << nanoseconds << " ns";
    return output.str();
}

void appendNode(std::ostringstream& output, const ProfileNode& node, std::string_view indent, bool last,
                std::uint64_t parentNanoseconds) {
    const double percent = parentNanoseconds == 0 ? 0.0
                                                  : 100.0 * static_cast<double>(node.inclusiveNanoseconds) /
                                                            static_cast<double>(parentNanoseconds);
    const std::uint64_t average = node.callCount == 0 ? 0 : node.inclusiveNanoseconds / node.callCount;
    output << indent << (last ? "`- " : "|- ") << node.name << "  calls=" << node.callCount
           << "  total=" << durationText(node.inclusiveNanoseconds) << "  self=" << durationText(node.selfNanoseconds)
           << "  avg=" << durationText(average) << "  " << std::fixed << std::setprecision(1) << percent << "%\n";

    const std::string childIndent = std::string(indent) + (last ? "   " : "|  ");
    for (std::size_t i = 0; i < node.children.size(); ++i)
        appendNode(output, node.children[i], childIndent, i + 1 == node.children.size(), node.inclusiveNanoseconds);
}

std::string htmlEscape(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '\"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result += character; break;
        }
    }
    return result;
}

void appendHtmlNode(std::ostringstream& output, const ProfileNode& node, std::uint64_t parentNanoseconds,
                    std::size_t depth) {
    const double percent = parentNanoseconds == 0 ? 0.0
                                                  : 100.0 * static_cast<double>(node.inclusiveNanoseconds) /
                                                            static_cast<double>(parentNanoseconds);
    const std::uint64_t average = node.callCount == 0 ? 0 : node.inclusiveNanoseconds / node.callCount;
    output << "<details class=\"node\"" << (depth < 2 ? " open" : "") << "><summary>"
           << "<span class=\"zone\"><span class=\"bar\" style=\"width:" << std::clamp(percent, 0.0, 100.0)
           << "%\"></span><span class=\"label\">" << htmlEscape(node.name) << "</span></span>"
           << "<span>" << node.callCount << "</span><span>" << durationText(node.inclusiveNanoseconds)
           << "</span><span>" << durationText(node.selfNanoseconds) << "</span><span>" << durationText(average)
           << "</span><span>" << std::fixed << std::setprecision(1) << percent << "%</span></summary>";
    if (!node.children.empty()) {
        output << "<div class=\"children\">";
        for (const ProfileNode& child : node.children)
            appendHtmlNode(output, child, node.inclusiveNanoseconds, depth + 1);
        output << "</div>";
    }
    output << "</details>";
}

std::string sessionName() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream output;
    output << std::put_time(&local, "%Y%m%d-%H%M%S") << "-p" << _getpid();
    return output.str();
}

}  // namespace

ScopedZone::ScopedZone(const char* name) noexcept {
    try {
        Registry& source = registry();
        if (!source.capturing.load(std::memory_order_acquire))
            return;
        ThreadState& state = currentThreadState();
        std::lock_guard lock(state.mutex);
        if (!source.capturing.load(std::memory_order_relaxed))
            return;
        const std::uint64_t currentEpoch = source.epoch.load(std::memory_order_relaxed);
        if (state.epoch != currentEpoch) {
            // 上一会话被截断的长作用域退出前，不把新会话嵌套进旧树。
            if (!state.stack.empty())
                return;
            state.roots.clear();
            state.epoch = currentEpoch;
        }
        std::vector<NodeData>& siblings = state.stack.empty() ? state.roots : state.stack.back().node->children;
        NodeData& node = findOrAdd(siblings, name);
        state.stack.push_back(ActiveZone{ .node = &node, .startedAt = Clock::now() });
        threadState_ = &state;
    } catch (...) {
        threadState_ = nullptr;
    }
}

ScopedZone::~ScopedZone() {
    if (threadState_ == nullptr)
        return;
    auto& state = *static_cast<ThreadState*>(threadState_);
    std::lock_guard lock(state.mutex);
    if (state.stack.empty())
        return;

    ActiveZone zone = state.stack.back();
    state.stack.pop_back();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - zone.startedAt).count();
    const std::uint64_t nanoseconds = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0;
    zone.node->callCount += 1;
    zone.node->inclusiveNanoseconds += nanoseconds;
    zone.node->selfNanoseconds += nanoseconds >= zone.childNanoseconds ? nanoseconds - zone.childNanoseconds : 0;
    if (!state.stack.empty())
        state.stack.back().childNanoseconds += nanoseconds;
}

void setThreadName(std::string_view name) {
    ThreadState& state = currentThreadState();
    std::lock_guard lock(state.mutex);
    state.name.assign(name);
}

void markFrame() noexcept {
    Registry& source = registry();
    if (source.capturing.load(std::memory_order_relaxed))
        source.frameCount.fetch_add(1, std::memory_order_relaxed);
}

void startCapture() {
    Registry& source = registry();
    source.capturing.store(false, std::memory_order_release);
    reset();
    source.capturing.store(true, std::memory_order_release);
}

ProfileSnapshot stopCapture() {
    registry().capturing.store(false, std::memory_order_release);
    return snapshot();
}

bool isCapturing() noexcept {
    return registry().capturing.load(std::memory_order_acquire);
}

void reset() {
    registry().frameCount.store(0, std::memory_order_relaxed);
    registry().epoch.fetch_add(1, std::memory_order_acq_rel);
}

ProfileSnapshot snapshot() {
    Registry& source = registry();
    ProfileSnapshot result;
    result.frameCount = source.frameCount.load(std::memory_order_relaxed);
    const std::uint64_t currentEpoch = source.epoch.load(std::memory_order_acquire);

    std::vector<std::shared_ptr<ThreadState>> threads;
    {
        std::lock_guard lock(source.mutex);
        threads = source.threads;
    }
    for (const auto& state : threads) {
        std::lock_guard lock(state->mutex);
        if (state->epoch != currentEpoch || state->roots.empty())
            continue;
        ThreadProfile thread{ .name = state->name, .id = state->id };
        thread.roots.reserve(state->roots.size());
        for (const NodeData& root : state->roots)
            thread.roots.push_back(copyNode(root));
        result.threads.push_back(std::move(thread));
    }
    return result;
}

std::string formatTree(const ProfileSnapshot& value) {
    std::ostringstream output;
    output << "Mulan Profiler: " << value.threads.size() << " thread(s), " << value.frameCount << " frame(s)\n";
    for (const ThreadProfile& thread : value.threads) {
        std::uint64_t total = 0;
        for (const ProfileNode& root : thread.roots)
            total += root.inclusiveNanoseconds;
        output << "\nThread \"" << thread.name << "\" (" << thread.id << ")  total=" << durationText(total) << "\n";
        for (std::size_t i = 0; i < thread.roots.size(); ++i)
            appendNode(output, thread.roots[i], "", i + 1 == thread.roots.size(), total);
    }
    return output.str();
}

std::string formatHtml(const ProfileSnapshot& value) {
    std::ostringstream output;
    output << R"HTML(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Mulan Profiler</title>
<style>
:root{color-scheme:dark;--bg:#0c111b;--panel:#141c29;--line:#263247;--text:#e7edf7;--muted:#8fa0b8;--hot:#ff7452;--warm:#f6bd4b;--cool:#47b5ff}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 15% 0,#17243a 0,transparent 38%),var(--bg);color:var(--text);font:13px/1.45 Inter,"Segoe UI",sans-serif}
main{max-width:1500px;margin:auto;padding:30px}h1{font-size:27px;margin:0 0 4px}.sub{color:var(--muted);margin-bottom:22px}.tools{display:flex;gap:9px;position:sticky;top:0;z-index:5;padding:12px 0;background:linear-gradient(var(--bg) 75%,transparent)}
input,button{border:1px solid var(--line);background:#101827;color:var(--text);border-radius:8px;padding:8px 11px}input{width:min(460px,60vw)}button{cursor:pointer}.thread{border:1px solid var(--line);background:rgba(20,28,41,.94);border-radius:12px;margin:13px 0;overflow:hidden;box-shadow:0 12px 35px #0004}.thread>summary{font-size:15px;font-weight:650;padding:15px 17px;cursor:pointer}.thread-body{padding:0 12px 14px;overflow-x:auto}
.columns,summary{display:grid;grid-template-columns:minmax(310px,1fr) 75px 100px 100px 100px 72px;gap:9px;align-items:center;min-width:820px}.columns{padding:8px 10px;color:var(--muted);font-size:11px;text-transform:uppercase;border-bottom:1px solid var(--line)}.columns span:not(:first-child),.node summary>span:not(:first-child){text-align:right;font-variant-numeric:tabular-nums}
.node{border-bottom:1px solid #202b3c}.node:last-child{border-bottom:0}.node>summary{padding:7px 10px;cursor:pointer;list-style:none}.node>summary::-webkit-details-marker{display:none}.node>summary:hover{background:#ffffff08}.zone{position:relative;overflow:hidden;padding:3px 7px;border-radius:4px}.bar{position:absolute;inset:0 auto 0 0;background:linear-gradient(90deg,#2389d455,#ff745244);border-right:1px solid var(--hot)}.label{position:relative;white-space:nowrap}.children{margin-left:20px;border-left:1px solid var(--line);padding-left:5px}.empty{padding:35px;color:var(--muted);text-align:center}[hidden]{display:none!important}
@media(max-width:700px){main{padding:18px 10px}.tools{flex-wrap:wrap}}
</style></head><body><main><h1>Mulan Profiler</h1>)HTML";
    output << "<div class=\"sub\">" << value.threads.size() << " thread(s) &middot; " << value.frameCount
           << " frame(s) &middot; generated " << htmlEscape(sessionName()) << "</div>";
    output << R"HTML(<div class="tools"><input id="search" type="search" placeholder="搜索函数或区域名称…"><button id="expand">全部展开</button><button id="collapse">全部折叠</button></div>)HTML";
    if (value.threads.empty())
        output << "<div class=\"thread empty\">没有采集到性能数据</div>";
    for (const ThreadProfile& thread : value.threads) {
        std::uint64_t total = 0;
        for (const ProfileNode& root : thread.roots)
            total += root.inclusiveNanoseconds;
        output << "<details class=\"thread\" open><summary>" << htmlEscape(thread.name)
               << " &nbsp;<span class=\"sub\">id=" << thread.id << " &middot; " << durationText(total)
               << "</span></summary><div class=\"thread-body\">"
               << "<div "
                  "class=\"columns\"><span>Zone</span><span>Calls</span><span>Total</span><span>Self</"
                  "span><span>Average</span><span>Parent</span></div>";
        for (const ProfileNode& root : thread.roots)
            appendHtmlNode(output, root, total, 0);
        output << "</div></details>";
    }
    output << R"HTML(<script>
const nodes=[...document.querySelectorAll('.node')];
document.querySelector('#expand').onclick=()=>document.querySelectorAll('details').forEach(x=>x.open=true);
document.querySelector('#collapse').onclick=()=>{nodes.forEach(x=>x.open=false);document.querySelectorAll('.thread').forEach(x=>x.open=true)};
document.querySelector('#search').oninput=e=>{const q=e.target.value.trim().toLowerCase();[...nodes].reverse().forEach(n=>{const own=n.querySelector(':scope > summary .label').textContent.toLowerCase().includes(q);const child=[...n.querySelectorAll(':scope > .children > .node')].some(x=>!x.hidden);n.hidden=!!q&&!own&&!child;if(q&&child)n.open=true})};
</script></main></body></html>)HTML";
    return output.str();
}

bool writeTextReport(std::string_view path) {
    std::ofstream output(std::string(path), std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output << formatTree(snapshot());
    return output.good();
}

bool writeHtmlReport(std::string_view path) {
    return writeHtmlReport(snapshot(), path);
}

bool writeHtmlReport(const ProfileSnapshot& value, std::string_view path) {
    std::ofstream output(std::string(path), std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output << formatHtml(value);
    return output.good();
}

std::string writeHtmlReportToDirectory(std::string_view rootPath) {
    return writeHtmlReportToDirectory(snapshot(), rootPath);
}

std::string writeHtmlReportToDirectory(const ProfileSnapshot& value, std::string_view rootPath) {
    if (rootPath.empty())
        return {};
    std::error_code error;
    const std::filesystem::path root{ std::string(rootPath) };
    std::filesystem::create_directories(root, error);
    if (error)
        return {};

    const std::string baseName = sessionName();
    for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
        const std::string name = suffix == 0 ? baseName : baseName + "-" + std::to_string(suffix);
        const std::filesystem::path session = root / name;
        error.clear();
        if (!std::filesystem::create_directory(session, error)) {
            if (error)
                return {};
            continue;
        }
        const std::filesystem::path report = session / "report.html";
        return writeHtmlReport(value, report.string()) ? report.string() : std::string{};
    }
    return {};
}

}  // namespace mulan::profiling
