#pragma once

#include "TtfFont.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace font {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    void Resize(int w, int h);
    void Clear(uint32_t bgra);
    void BlendPixel(int x, int y, uint8_t alpha, uint32_t color);
};

class FontRenderer {
public:
    explicit FontRenderer(const TtfFont& font);

    void DrawString(Image& image, const std::wstring& text, double x, double baselineY, double pixelSize, uint32_t bgra) const;
    void DrawGlyph(Image& image, uint16_t glyphIndex, double x, double baselineY, double pixelSize, uint32_t bgra) const;

private:
    struct CachedGlyph {
        int width = 0;
        int height = 0;
        int offsetX = 0;
        int offsetY = 0;
        uint16_t advanceWidth = 0;
        std::vector<uint8_t> alpha;
    };

    struct CacheKey {
        uint16_t glyphIndex = 0;
        int pixelSizeTenths = 0;

        bool operator==(const CacheKey& other) const
        {
            return glyphIndex == other.glyphIndex && pixelSizeTenths == other.pixelSizeTenths;
        }
    };

    struct CacheKeyHash {
        size_t operator()(const CacheKey& key) const
        {
            return (static_cast<size_t>(key.glyphIndex) << 16) ^ static_cast<size_t>(key.pixelSizeTenths);
        }
    };

    const CachedGlyph& RasterizeGlyph(uint16_t glyphIndex, double pixelSize) const;

    const TtfFont& font_;
    mutable std::unordered_map<CacheKey, CachedGlyph, CacheKeyHash> glyphCache_;
};

} // namespace font
