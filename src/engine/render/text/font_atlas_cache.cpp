#include "font_atlas_cache.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace mulan::engine {
namespace {

constexpr uint32_t kCacheVersion = 1;
constexpr uint32_t kHeaderSize = 80;
constexpr uint32_t kPixelFormatRgba8 = 1;
constexpr uint32_t kEndianMarker = 0x01020304u;
constexpr uint64_t kMaxAtlasBytes = 256ull * 1024ull * 1024ull;
constexpr uint32_t kMaxGlyphCount = 1u << 20u;
constexpr char kMagic[8] = { 'M', 'F', 'A', 'T', 'L', 'A', 'S', 0 };

uint64_t fnv1a64Bytes(const void* data, size_t size, uint64_t hash = 14695981039346656037ull) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t fnv1a64String(std::string_view text, uint64_t hash = 14695981039346656037ull) {
    return fnv1a64Bytes(text.data(), text.size(), hash);
}

uint64_t hashFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return 0;
    }

    uint64_t hash = 14695981039346656037ull;
    char buffer[64 * 1024];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize read = in.gcount();
        if (read > 0) {
            hash = fnv1a64Bytes(buffer, static_cast<size_t>(read), hash);
        }
    }
    return hash;
}

std::string hex64(uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<size_t>(i)] = digits[value & 0xFu];
        value >>= 4u;
    }
    return out;
}

void appendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

void appendU64(std::vector<uint8_t>& out, uint64_t value) {
    for (uint32_t i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

void appendF32(std::vector<uint8_t>& out, float value) {
    appendU32(out, std::bit_cast<uint32_t>(value));
}

bool readExact(std::istream& in, void* data, size_t size) {
    in.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return in.good() || static_cast<size_t>(in.gcount()) == size;
}

bool readU32(const std::vector<uint8_t>& data, size_t& offset, uint32_t& out) {
    if (offset + 4 > data.size()) {
        return false;
    }
    out = static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
          (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool readF32(const std::vector<uint8_t>& data, size_t& offset, float& out) {
    uint32_t bits = 0;
    if (!readU32(data, offset, bits)) {
        return false;
    }
    out = std::bit_cast<float>(bits);
    return true;
}

bool readHeaderU32(std::istream& in, uint32_t& out) {
    uint8_t bytes[4]{};
    if (!readExact(in, bytes, sizeof(bytes))) {
        return false;
    }
    out = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
          (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

bool readHeaderU64(std::istream& in, uint64_t& out) {
    uint8_t bytes[8]{};
    if (!readExact(in, bytes, sizeof(bytes))) {
        return false;
    }
    out = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        out |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    }
    return true;
}

bool readHeaderF32(std::istream& in, float& out) {
    uint32_t bits = 0;
    if (!readHeaderU32(in, bits)) {
        return false;
    }
    out = std::bit_cast<float>(bits);
    return true;
}

std::vector<uint8_t> buildPayload(const FontAtlasCpuData& data) {
    std::vector<GlyphInfo> glyphs;
    glyphs.reserve(data.glyphs.size());
    for (const auto& item : data.glyphs) {
        glyphs.push_back(item.second);
    }
    std::sort(glyphs.begin(), glyphs.end(),
              [](const GlyphInfo& a, const GlyphInfo& b) { return a.unicode < b.unicode; });

    std::vector<uint8_t> payload;
    payload.reserve(glyphs.size() * (sizeof(uint32_t) + sizeof(float) * 10u) + data.rgbaPixels.size());
    for (const GlyphInfo& glyph : glyphs) {
        appendU32(payload, glyph.unicode);
        appendF32(payload, glyph.advanceX);
        appendF32(payload, glyph.advanceY);
        appendF32(payload, glyph.atlasU);
        appendF32(payload, glyph.atlasV);
        appendF32(payload, glyph.atlasU2);
        appendF32(payload, glyph.atlasV2);
        appendF32(payload, glyph.planeLeft);
        appendF32(payload, glyph.planeTop);
        appendF32(payload, glyph.width);
        appendF32(payload, glyph.height);
    }
    appendBytes(payload, data.rgbaPixels.data(), data.rgbaPixels.size());
    return payload;
}

bool parsePayload(const std::vector<uint8_t>& payload, uint32_t glyphCount, uint64_t pixelBytes,
                  FontAtlasCpuData& outData) {
    const uint64_t glyphBytes = static_cast<uint64_t>(glyphCount) * (sizeof(uint32_t) + sizeof(float) * 10ull);
    if (glyphCount > kMaxGlyphCount || pixelBytes > kMaxAtlasBytes || glyphBytes + pixelBytes != payload.size()) {
        return false;
    }

    size_t offset = 0;
    outData.glyphs.clear();
    outData.glyphs.reserve(glyphCount);
    for (uint32_t i = 0; i < glyphCount; ++i) {
        GlyphInfo glyph;
        if (!readU32(payload, offset, glyph.unicode) || !readF32(payload, offset, glyph.advanceX) ||
            !readF32(payload, offset, glyph.advanceY) || !readF32(payload, offset, glyph.atlasU) ||
            !readF32(payload, offset, glyph.atlasV) || !readF32(payload, offset, glyph.atlasU2) ||
            !readF32(payload, offset, glyph.atlasV2) || !readF32(payload, offset, glyph.planeLeft) ||
            !readF32(payload, offset, glyph.planeTop) || !readF32(payload, offset, glyph.width) ||
            !readF32(payload, offset, glyph.height)) {
            return false;
        }
        outData.glyphs.emplace(glyph.unicode, glyph);
    }

    outData.rgbaPixels.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.end());
    return true;
}

std::filesystem::path environmentPath(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] ? std::filesystem::path(value) : std::filesystem::path();
}

}  // namespace

FontAtlasCacheKey FontAtlasCache::makeKey(const char* fontPath, float fontSize, uint32_t atlasWidth,
                                          uint32_t atlasHeight, uint64_t charsetHash) {
    FontAtlasCacheKey key;
    key.fontSize = fontSize;
    key.requestedAtlasWidth = atlasWidth;
    key.requestedAtlasHeight = atlasHeight;
    key.charsetHash = charsetHash;

    std::error_code ec;
    std::filesystem::path path(fontPath ? fontPath : "");
    key.fontPath = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        ec.clear();
        key.fontPath = std::filesystem::absolute(path, ec);
        if (ec) {
            key.fontPath = path;
        }
    }

    key.fontFileSize = std::filesystem::file_size(key.fontPath, ec);
    if (ec) {
        key.fontFileSize = 0;
    }
    key.fontContentHash = hashFile(key.fontPath);

    std::ostringstream ss;
    ss << "mulan-font-atlas-cache-v" << kCacheVersion << '\n';
    ss << key.fontPath.generic_string() << '\n';
    ss << key.fontFileSize << '\n';
    ss << key.fontContentHash << '\n';
    ss << key.charsetHash << '\n';
    ss << std::bit_cast<uint32_t>(fontSize) << '\n';
    ss << atlasWidth << '\n';
    ss << atlasHeight << '\n';
    ss << kPixelFormatRgba8 << '\n';

    const std::string keyText = ss.str();
    key.keyHash = fnv1a64String(keyText);
    key.filePath = cacheDirectory() / ("font-" + hex64(key.keyHash) + ".mfatlas");
    return key;
}

bool FontAtlasCache::tryLoad(const FontAtlasCacheKey& key, FontAtlasCpuData& outData) {
    std::ifstream in(key.filePath, std::ios::binary);
    if (!in) {
        return false;
    }

    char magic[8]{};
    if (!readExact(in, magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }

    uint32_t version = 0;
    uint32_t headerSize = 0;
    uint32_t endian = 0;
    uint32_t pixelFormat = 0;
    uint32_t atlasWidth = 0;
    uint32_t atlasHeight = 0;
    float baseFontSize = 0.0f;
    float pxRange = 0.0f;
    uint32_t glyphCount = 0;
    uint64_t pixelBytes = 0;
    uint64_t charsetHash = 0;
    uint64_t payloadHash = 0;
    uint64_t keyHash = 0;

    if (!readHeaderU32(in, version) || !readHeaderU32(in, headerSize) || !readHeaderU32(in, endian) ||
        !readHeaderU32(in, pixelFormat) || !readHeaderU32(in, atlasWidth) || !readHeaderU32(in, atlasHeight) ||
        !readHeaderF32(in, baseFontSize) || !readHeaderF32(in, pxRange) || !readHeaderU32(in, glyphCount) ||
        !readHeaderU64(in, pixelBytes) || !readHeaderU64(in, charsetHash) || !readHeaderU64(in, payloadHash) ||
        !readHeaderU64(in, keyHash)) {
        return false;
    }

    if (version != kCacheVersion || headerSize != kHeaderSize || endian != kEndianMarker ||
        pixelFormat != kPixelFormatRgba8 || charsetHash != key.charsetHash || keyHash != key.keyHash ||
        std::bit_cast<uint32_t>(baseFontSize) != std::bit_cast<uint32_t>(key.fontSize)) {
        return false;
    }
    if (atlasWidth == 0 || atlasHeight == 0 || pixelBytes != static_cast<uint64_t>(atlasWidth) * atlasHeight * 4ull ||
        pixelBytes > kMaxAtlasBytes) {
        return false;
    }

    const uint64_t glyphBytes = static_cast<uint64_t>(glyphCount) * (sizeof(uint32_t) + sizeof(float) * 10ull);
    if (glyphCount > kMaxGlyphCount || glyphBytes + pixelBytes > kMaxAtlasBytes + glyphBytes) {
        return false;
    }

    std::vector<uint8_t> payload(static_cast<size_t>(glyphBytes + pixelBytes));
    if (!readExact(in, payload.data(), payload.size()) || fnv1a64Bytes(payload.data(), payload.size()) != payloadHash) {
        return false;
    }

    FontAtlasCpuData data;
    data.baseFontSize = baseFontSize;
    data.pxRange = pxRange;
    data.atlasWidth = atlasWidth;
    data.atlasHeight = atlasHeight;
    data.charsetHash = charsetHash;
    if (!parsePayload(payload, glyphCount, pixelBytes, data)) {
        return false;
    }

    outData = std::move(data);
    return true;
}

bool FontAtlasCache::save(const FontAtlasCacheKey& key, const FontAtlasCpuData& data) {
    if (key.filePath.empty() || data.atlasWidth == 0 || data.atlasHeight == 0 || data.rgbaPixels.empty() ||
        data.charsetHash != key.charsetHash) {
        return false;
    }

    const uint64_t pixelBytes = static_cast<uint64_t>(data.rgbaPixels.size());
    if (pixelBytes != static_cast<uint64_t>(data.atlasWidth) * data.atlasHeight * 4ull || pixelBytes > kMaxAtlasBytes ||
        data.glyphs.size() > kMaxGlyphCount) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(key.filePath.parent_path(), ec);
    if (ec) {
        return false;
    }

    const std::vector<uint8_t> payload = buildPayload(data);
    const uint64_t payloadHash = fnv1a64Bytes(payload.data(), payload.size());
    const auto tempPath = key.filePath.string() + ".tmp";

    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    out.write(kMagic, sizeof(kMagic));
    std::vector<uint8_t> header;
    appendU32(header, kCacheVersion);
    appendU32(header, kHeaderSize);
    appendU32(header, kEndianMarker);
    appendU32(header, kPixelFormatRgba8);
    appendU32(header, data.atlasWidth);
    appendU32(header, data.atlasHeight);
    appendF32(header, data.baseFontSize);
    appendF32(header, data.pxRange);
    appendU32(header, static_cast<uint32_t>(data.glyphs.size()));
    appendU64(header, pixelBytes);
    appendU64(header, data.charsetHash);
    appendU64(header, payloadHash);
    appendU64(header, key.keyHash);
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    out.close();

    if (!out) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    std::filesystem::rename(tempPath, key.filePath, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(key.filePath, ec);
        ec.clear();
        std::filesystem::rename(tempPath, key.filePath, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }
    return true;
}

std::filesystem::path FontAtlasCache::cacheDirectory() {
#ifdef _WIN32
    std::filesystem::path base = environmentPath("LOCALAPPDATA");
    if (base.empty()) {
        base = environmentPath("TEMP");
    }
    if (base.empty()) {
        base = std::filesystem::temp_directory_path();
    }
    return base / "Mulan" / "FontAtlasCache";
#else
    std::filesystem::path base = environmentPath("XDG_CACHE_HOME");
    if (base.empty()) {
        base = environmentPath("HOME");
        if (!base.empty()) {
            base /= ".cache";
        }
    }
    if (base.empty()) {
        base = std::filesystem::temp_directory_path();
    }
    return base / "mulan" / "font-atlas";
#endif
}

}  // namespace mulan::engine
