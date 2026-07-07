# Text Font Atlas Cache 方案

## 背景

当前 `FontAtlas::load()` 会在运行时从 TTF/OTF 字体文件现场生成 MSDF 字体图集：

1. 初始化 FreeType；
2. 加载字体文件；
3. 构造字符集；
4. 对每个 glyph 做 edge coloring；
5. pack glyph；
6. 生成 MSDF bitmap；
7. 转换为 RGBA8；
8. 创建 GPU Texture / Sampler；
9. 上传 atlas 到 GPU。

这意味着程序第一次进入文字渲染链路时会有明显阻塞。这里的“实时生成”指的是运行时生成文字用的 MSDF 字体图集，不是 glTF/CAD 材质贴图。

## 目标

- 避免每次启动都重新生成默认字体 MSDF atlas。
- 支持未来复杂文本：多字体、多字号、多语言、动态文字、3D 标注、测量标注。
- 保持 `TextStage` 边界干净，只负责消费 `TextDrawList`、布局、batch、绘制。
- 将字体文件、磁盘缓存、glyph 生成、runtime atlas 管理集中在 text 资源子系统内。
- 最终发布版支持预生成默认字体缓存，让用户第一次启动也不慢。

## 所属层级

该能力应放在 `engine/render/text/`，不应放在 `ViewCubeStage`、app 层或 view 层。

推荐结构：

```text
engine/render/text/
  FontManager
    对上层暴露字体注册、默认字体策略、glyph resolve。

  FontAtlas
    表示一份 CPU glyph table + GPU Texture/Sampler。

  FontAtlasCache
    负责磁盘缓存读写、cache key、版本、序列化、反序列化。

  DynamicGlyphAtlas
    负责运行时 glyph 分配、atlas page 管理、dirty rect 上传。

  TextStage
    只消费 TextDrawList，不直接关心字体文件、磁盘缓存、glyph 生成策略。
```

核心原则：

```text
TextStage -> FontManager -> FontAtlas / DynamicGlyphAtlas -> RHI
                         -> FontAtlasCache
```

`FontManager` 是 facade；`TextStage` 是消费者。

## 阶段路线

### 阶段 1：磁盘缓存，cache miss 同步生成

这是近期最值得做的一步。

流程：

```text
FontManager::loadFont()
  -> FontAtlasCache::tryLoad(cacheKey)
  -> 命中：读取 atlas pixels + glyph metrics
  -> 创建 GPU Texture / Sampler
  -> 未命中：调用现有 FontAtlas::load() 生成
  -> 保存 cache
```

收益：

- 第二次启动不再跑 msdf-atlas-gen；
- 当前 `FontAtlas` 结构改动可控；
- 不改变 `TextStage` 的绘制模型；
- 可以先服务默认字体和 ViewCube 文字。

注意：

- 第一次 cache miss 仍然会慢；
- 这是开发期可接受的第一步；
- 发布版要配合预生成 cache 才能做到用户第一次启动也快。

### 阶段 2：默认字体预生成工具

增加一个离线工具或构建步骤：

```text
TTF + charset + msdf params -> .mfatlas cache
```

发布包携带默认 cache：

```text
assets/fonts/cache/default-*.mfatlas
```

收益：

- 用户第一次启动也不需要生成默认字体 atlas；
- 默认字体资源可版本化；
- 启动体验稳定。

### 阶段 3：运行时动态 glyph cache

当文字内容不固定、多语言或用户输入变多时，再引入。

流程：

```text
TextStage 收到 TextDrawDesc
  -> FontManager::resolveGlyphs(fontKey, text)
  -> 已存在 glyph：直接布局
  -> 缺 glyph：加入 pending glyph 请求
  -> DynamicGlyphAtlas 分配空位
  -> msdfgen 只生成缺失 glyph
  -> 更新 CPU glyph table
  -> RHI 局部上传 dirty rect
  -> 下帧使用新 glyph 绘制
```

不建议一开始做 LRU 淘汰。更适合本项目的是多 page atlas：

```text
FontAtlasPage 0
FontAtlasPage 1
FontAtlasPage 2
...
```

原因：

- CAD/模型查看器更重视稳定性；
- 文本量通常不是无限增长；
- 多 page 比 LRU 淘汰更容易保证 glyph handle 稳定。

### 阶段 4：多字体、多字号、多样式

引入稳定的 font key：

```cpp
struct FontKey {
    std::string family;
    float sizePx = 48.0f;
    uint32_t weight = 400;
    bool italic = false;
};
```

未来可以将 `TextDrawDesc::font` 从简单 string 逐步升级为更明确的 font/style handle。

## Cache Key 设计

磁盘缓存 key 至少应包含：

- engine font cache version；
- font file path 或 font family id；
- font file timestamp / size / content hash；
- font size；
- atlas generation params；
- MSDF px range；
- charset hash；
- msdf-atlas-gen 参数版本；
- atlas format。

建议先用保守 key，避免错误复用旧缓存：

```text
cacheKey = hash(
  version,
  fontPath,
  fontFileSize,
  fontFileTimestamp,
  fontSize,
  pxRange,
  charsetHash,
  generatorParams
)
```

后续如果要更稳定，可以改为 font file content hash。

## Cache 文件内容

建议单文件二进制格式：

```text
Header
  magic
  version
  atlasWidth
  atlasHeight
  baseFontSize
  pxRange
  glyphCount
  pixelFormat

GlyphInfo[]

AtlasPixels
  RGBA8 或 RGB8
```

当前 shader 用 atlas RGB 作为 MSDF 通道，alpha 固定 1。短期继续保存 RGBA8 最简单；后续可改 RGB8 或压缩格式。

## 对外 API 草案

短期：

```cpp
class FontManager {
public:
    bool loadFont(const char* key, const char* fontPath, float fontSize = 48.0f, uint32_t atlasSize = 1024);
    FontAtlas* font(const char* key) const;
    FontAtlas* defaultFont() const;
};
```

中期：

```cpp
class FontManager {
public:
    FontHandle registerFont(const FontDesc& desc);
    FontAtlasSpan resolveGlyphs(const FontKey& key, std::u32string_view text);
    void flushPendingGlyphUploads(RHIDevice& device);
};
```

`TextStage` 每帧只做：

```text
resolve glyphs
layout text
group by atlas page
draw batches
```

## 首次启动慢的处理

必须区分开发期和发布期。

开发期：

```text
第一次 cache miss -> 同步生成 -> 写 cache
第二次启动 -> 读 cache
```

发布期：

```text
构建/打包阶段预生成默认 cache
用户第一次启动 -> 直接读 cache
```

如果运行时遇到新字符：

```text
优先不阻塞主渲染路径
后台生成 pending glyph
生成完成后局部上传
下一帧生效
```

对于 ViewCube 这种关键文字，可以使用极小固定字符集预置缓存，确保启动后立即可用。

## 与现有代码的落点

当前相关文件：

```text
src/engine/render/text/font_atlas.h
src/engine/render/text/font_atlas.cpp
src/engine/render/text/font_manager.h
src/engine/render/text/font_manager.cpp
src/engine/render/text/text_stage.h
src/engine/render/text/text_stage.cpp
```

建议新增：

```text
src/engine/render/text/font_atlas_cache.h
src/engine/render/text/font_atlas_cache.cpp
src/engine/render/text/dynamic_glyph_atlas.h
src/engine/render/text/dynamic_glyph_atlas.cpp
```

第一阶段只需要 `FontAtlasCache`，暂不引入 `DynamicGlyphAtlas`。

## 风险和注意事项

- 不要让 `TextStage` 直接读写磁盘缓存，否则 pass 会变重。
- 不要把 ViewCube 字体逻辑写死到 `ViewCubeStage`。
- 不要一开始做 LRU glyph 淘汰，glyph handle 稳定性更重要。
- cache key 必须保守，宁可 miss 后重新生成，也不要错误复用旧缓存。
- GPU Texture 更新需要走 RHI 抽象，不能让 Vulkan 细节泄漏到 text 层。
- 动态 glyph 生成最好允许异步，但第一阶段可以先同步。

## 推荐执行顺序

1. 保留当前同步 `FontAtlas::load()` 作为 fallback。
2. 新增 `FontAtlasCache`，支持读取/写入 atlas pixels + glyph metrics。
3. `FontManager::loadFont()` 先查 cache，miss 后生成并写 cache。
4. 加默认字体 cache 目录策略。
5. 后续再做预生成工具。
6. 最后再做 `DynamicGlyphAtlas` 和多 page atlas。

## 当前结论

短期最优解不是立刻做复杂动态 glyph cache，而是先把磁盘缓存打通：

```text
cache hit: 快速读取并上传
cache miss: 复用现有生成逻辑
```

这一步收益最大、风险最低，也不会污染现有 `TextStage` 边界。复杂 runtime glyph cache 可以在这个基础上继续演进。

## 基于当前代码的补充

当前文字链路已经比最初方案更接近正确分层：

```text
RenderRequest::textDraws
  -> RenderRenderer::executeStages()
  -> TextStage::addTextList()
  -> TextStage::buildGeometry()
  -> FontManager / FontAtlas
```

当前真实代码落点：

```text
src/engine/render/frontend/render_request.h
src/engine/render/backend/render_renderer.cpp
src/engine/render/text/text_stage.h
src/engine/render/text/text_stage.cpp
src/engine/render/text/font_manager.h
src/engine/render/text/font_manager.cpp
src/engine/render/text/font_atlas.h
src/engine/render/text/font_atlas.cpp
src/engine/render/text/text_layout.h
src/engine/render/text/text_layout.cpp
```

### 当前 TextStage 状态

`TextStage` 当前已经负责：

- 初始化文字 shader / pipeline；
- 创建动态 VB / IB；
- 创建文字参数 UBO；
- 每帧接收 `TextDrawList`；
- 根据 `TextDrawDesc` 生成文字顶点；
- 按字体 atlas 分 batch；
- 为不同字体 atlas 创建并缓存 `BindGroup`；
- 执行最终 draw。

`TextStage` 当前不应该继续增加：

- 字体文件查找策略；
- 磁盘 cache key 生成；
- atlas cache 读写；
- glyph 动态生成；
- cache 目录管理；
- 预生成工具逻辑。

这些应继续下沉到 `FontManager / FontAtlasCache / DynamicGlyphAtlas`。

### 当前 FontManager 状态

`FontManager` 当前是 renderer-owned 对象，由 `TextStage` 持有：

```text
TextStage
  -> std::unique_ptr<FontManager> font_manager_
```

当前接口很薄：

```cpp
bool loadFont(const char* key, const char* fontPath, float fontSize, uint32_t atlasSize);
FontAtlas* font(const char* key) const;
FontAtlas* defaultFont() const;
```

第一阶段 cache 应优先接在这里，而不是接在 `TextStage`：

```text
FontManager::loadFont()
  -> 如果 key 已存在，直接返回 true
  -> 计算 FontCacheKey
  -> FontAtlasCache::tryLoad()
  -> cache hit: FontAtlas::loadFromCachedData()
  -> cache miss: FontAtlas::load() 走现有同步生成
  -> FontAtlasCache::save()
```

这样 `TextStage` 的使用方式不变。

### 当前 FontAtlas 状态

`FontAtlas::load()` 当前同时做了三类事情：

```text
1. 字体/MSDF CPU 生成
2. glyph metrics 填充
3. GPU texture/sampler 创建和上传
```

为了接 cache，建议拆出两个内部能力：

```cpp
struct FontAtlasCpuData {
    uint32_t width;
    uint32_t height;
    float baseFontSize;
    float pxRange;
    std::unordered_map<uint32_t, GlyphInfo> glyphs;
    std::vector<uint8_t> rgbaPixels;
};

bool FontAtlas::loadFromCpuData(FontAtlasCpuData data);
```

然后现有 `load()` 变成：

```text
generate CpuData
-> loadFromCpuData()
```

cache hit 时变成：

```text
read CpuData
-> loadFromCpuData()
```

收益：

- 生成路径和 cache 读取路径共用 GPU 上传逻辑；
- 不重复写 texture/sampler 创建代码；
- cache 文件不需要知道 RHI 后端；
- Vulkan/D3D12/WebGPU 后端仍只通过 `RHIDevice` 进入。

### 当前默认字体加载点

当前默认字体在：

```text
TextStage::init()
  -> font_manager_ = std::make_unique<FontManager>(device)
  -> loadDefaultFont()
```

`loadDefaultFont()` 会查找系统字体：

```text
Windows:
  C:/Windows/Fonts/segoeui.ttf
  C:/Windows/Fonts/arial.ttf
  C:/Windows/Fonts/calibri.ttf
```

这意味着第一阶段 cache key 必须包含真实字体路径和字体文件信息，否则不同机器上的默认字体可能错误复用 cache。

### 当前字符集状态

当前 `FontAtlas::load()` 生成的字符集是：

```text
ASCII 32~126
常用中文标点：
  0x3001
  0x3002
  0xFF0C
  0xFF0E
  0xFF08
  0xFF09
  0x2018
  0x2019
  0x201C
  0x201D
```

因此第一阶段 cache key 还必须包含 charset hash。否则未来扩展字符集后，旧 cache 会缺 glyph。

### 当前 GPU 上传状态

`FontAtlas::uploadAtlas()` 当前通过 RHI：

```text
TextureUsageFlags::ShaderResource | TextureUsageFlags::TransferDst
device_->createTexture()
device_->uploadTextureData()
```

这条路径是正确方向。`FontAtlasCache` 不应直接接触 Vulkan layout、barrier、staging buffer。

后续动态 glyph 局部更新时，需要 RHI 提供更明确的局部 texture upload 接口，例如：

```cpp
uploadTextureRegion(Texture* texture, const TextureUploadRegion& region, const void* data);
```

第一阶段磁盘 cache 不需要这个接口，整张上传即可。

### 第一阶段最小改动清单

建议真正实现时按这个顺序：

1. 新增 `FontAtlasCpuData`。
2. 把 `FontAtlas::load()` 中的 MSDF 生成结果先落到 `FontAtlasCpuData`。
3. 新增 `FontAtlas::loadFromCpuData()`，统一创建 texture/sampler。
4. 新增 `FontAtlasCache`，只读写 `FontAtlasCpuData`。
5. `FontManager::loadFont()` 增加：
   - key 已存在直接返回；
   - cache hit 读取；
   - cache miss 生成并保存。
6. `TextStage` 不改或只改很少。

### 暂时不做的内容

第一阶段不要做：

- 多 page atlas；
- runtime missing glyph 异步生成；
- LRU glyph 淘汰；
- GPU 局部 dirty rect 上传；
- 多字体 fallback 链；
- UI 层字体配置入口。

这些都应该等磁盘 cache 跑通后再接。
