#include "FontRenderer.h"

#include <algorithm>
#include <cmath>

namespace font {

void Image::Resize(int w, int h)
{
    width = w;
    height = h;
    pixels.assign(static_cast<size_t>(w) * h, 0);
}

void Image::Clear(uint32_t bgra)
{
    std::fill(pixels.begin(), pixels.end(), bgra);
}

void Image::BlendPixel(int x, int y, uint8_t alpha, uint32_t color)
{
    if (x < 0 || y < 0 || x >= width || y >= height || alpha == 0) return;

    uint32_t& dst = pixels[static_cast<size_t>(y) * width + x];
    const uint32_t inv = 255 - alpha;

    const uint32_t sb = color & 0xFF;
    const uint32_t sg = (color >> 8) & 0xFF;
    const uint32_t sr = (color >> 16) & 0xFF;

    const uint32_t db = dst & 0xFF;
    const uint32_t dg = (dst >> 8) & 0xFF;
    const uint32_t dr = (dst >> 16) & 0xFF;

    const uint32_t b = (sb * alpha + db * inv) / 255;
    const uint32_t g = (sg * alpha + dg * inv) / 255;
    const uint32_t r = (sr * alpha + dr * inv) / 255;
    dst = 0xFF000000 | (r << 16) | (g << 8) | b;
}

FontRenderer::FontRenderer(const TtfFont& font)
    : font_(font)
{
}

void FontRenderer::DrawString(Image& image, const std::wstring& text, double x, double baselineY, double pixelSize, uint32_t bgra) const
{
    const double scale = pixelSize / font_.UnitsPerEm();
    double penX = x;

    for (wchar_t ch : text) {
        if (ch == L'\n') {
            penX = x;
            baselineY += (font_.Ascender() - font_.Descender() + font_.LineGap()) * scale;
            continue;
        }

        const uint16_t glyphIndex = font_.GlyphIndexForCodepoint(static_cast<uint32_t>(ch));
        const CachedGlyph& glyph = RasterizeGlyph(glyphIndex, pixelSize);
        DrawGlyph(image, glyphIndex, penX, baselineY, pixelSize, bgra);
        penX += glyph.advanceWidth * scale;
    }
}

void FontRenderer::DrawGlyph(Image& image, uint16_t glyphIndex, double x, double baselineY, double pixelSize, uint32_t bgra) const
{
    const CachedGlyph& glyph = RasterizeGlyph(glyphIndex, pixelSize);
    if (glyph.alpha.empty()) return;

    const int dstX = static_cast<int>(std::floor(x)) + glyph.offsetX;
    const int dstY = static_cast<int>(std::floor(baselineY)) + glyph.offsetY;
    for (int y = 0; y < glyph.height; ++y) {
        for (int x = 0; x < glyph.width; ++x) {
            const uint8_t alpha = glyph.alpha[static_cast<size_t>(y) * glyph.width + x];
            if (alpha != 0) image.BlendPixel(dstX + x, dstY + y, alpha, bgra);
        }
    }
}

const FontRenderer::CachedGlyph& FontRenderer::RasterizeGlyph(uint16_t glyphIndex, double pixelSize) const
{
    CacheKey key{ glyphIndex, static_cast<int>(std::lround(pixelSize * 10.0)) };
    auto found = glyphCache_.find(key);
    if (found != glyphCache_.end()) return found->second;

    CachedGlyph cached;
    GlyphOutline outline;
    if (!font_.LoadGlyph(glyphIndex, outline)) {
        auto inserted = glyphCache_.emplace(key, std::move(cached));
        return inserted.first->second;
    }

    cached.advanceWidth = outline.advanceWidth;
    if (outline.contours.empty()) {
        auto inserted = glyphCache_.emplace(key, std::move(cached));
        return inserted.first->second;
    }

    const double scale = pixelSize / font_.UnitsPerEm();
    std::vector<ScreenContour> contours;
    contours.reserve(outline.contours.size());

    double minX = 1e9;
    double minY = 1e9;
    double maxX = -1e9;
    double maxY = -1e9;

    for (const Contour& source : outline.contours) {
        ScreenContour dest;
        dest.points.reserve(source.points.size());
        for (const Vec2& p : source.points) {
            Vec2 screen{ p.x * scale, -p.y * scale };
            minX = std::min(minX, screen.x);
            minY = std::min(minY, screen.y);
            maxX = std::max(maxX, screen.x);
            maxY = std::max(maxY, screen.y);
            dest.points.push_back(screen);
        }
        contours.push_back(dest);
    }

    const int x0 = static_cast<int>(std::floor(minX)) - 1;
    const int y0 = static_cast<int>(std::floor(minY)) - 1;
    const int x1 = static_cast<int>(std::ceil(maxX)) + 1;
    const int y1 = static_cast<int>(std::ceil(maxY)) + 1;
    cached.offsetX = x0;
    cached.offsetY = y0;
    cached.width = std::max(0, x1 - x0 + 1);
    cached.height = std::max(0, y1 - y0 + 1);
    cached.alpha.assign(static_cast<size_t>(cached.width) * cached.height, 0);

    constexpr int samples = 4;
    constexpr int totalSamples = samples * samples;
    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            int covered = 0;
            for (int sy = 0; sy < samples; ++sy) {
                for (int sx = 0; sx < samples; ++sx) {
                    const double sampleX = px + (sx + 0.5) / samples;
                    const double sampleY = py + (sy + 0.5) / samples;
                    if (PointInside(contours, sampleX, sampleY)) ++covered;
                }
            }
            if (covered > 0) {
                const int localX = px - x0;
                const int localY = py - y0;
                cached.alpha[static_cast<size_t>(localY) * cached.width + localX] =
                    static_cast<uint8_t>(covered * 255 / totalSamples);
            }
        }
    }

    auto inserted = glyphCache_.emplace(key, std::move(cached));
    return inserted.first->second;
}

bool FontRenderer::PointInside(const std::vector<ScreenContour>& contours, double x, double y)
{
    bool inside = false;
    for (const ScreenContour& contour : contours) {
        if (contour.points.size() < 2) continue;
        for (size_t i = 0, j = contour.points.size() - 1; i < contour.points.size(); j = i++) {
            if (RayIntersects(contour.points[j], contour.points[i], x, y)) inside = !inside;
        }
    }
    return inside;
}

bool FontRenderer::RayIntersects(const Vec2& a, const Vec2& b, double x, double y)
{
    const bool crosses = ((a.y > y) != (b.y > y));
    if (!crosses) return false;
    const double atX = (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x;
    return x < atX;
}

} // namespace font
