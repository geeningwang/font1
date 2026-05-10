#include "FontRenderer.h"

#include <algorithm>
#include <cmath>

namespace font {

namespace {

struct Edge {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double yMin = 0.0;
    double yMax = 0.0;
};

void AccumulateSpan(std::vector<double>& coverage, int width, int row, double left, double right, int verticalSamples)
{
    if (right <= 0.0 || left >= width || right <= left) return;

    left = std::max(0.0, left);
    right = std::min(static_cast<double>(width), right);

    const int first = std::max(0, static_cast<int>(std::floor(left)));
    const int last = std::min(width - 1, static_cast<int>(std::ceil(right)) - 1);
    for (int x = first; x <= last; ++x) {
        const double coveredWidth = std::min(right, static_cast<double>(x + 1)) - std::max(left, static_cast<double>(x));
        if (coveredWidth > 0.0) {
            coverage[static_cast<size_t>(row) * width + x] += coveredWidth / verticalSamples;
        }
    }
}

} // namespace

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
    std::vector<Edge> edges;

    double minX = 1e9;
    double minY = 1e9;
    double maxX = -1e9;
    double maxY = -1e9;

    for (const Contour& source : outline.contours) {
        std::vector<Vec2> points;
        points.reserve(source.points.size());
        for (const Vec2& p : source.points) {
            Vec2 screen{ p.x * scale, -p.y * scale };
            minX = std::min(minX, screen.x);
            minY = std::min(minY, screen.y);
            maxX = std::max(maxX, screen.x);
            maxY = std::max(maxY, screen.y);
            points.push_back(screen);
        }

        if (points.size() < 2) continue;
        for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            const Vec2& a = points[j];
            const Vec2& b = points[i];
            if (a.y == b.y) continue;

            Edge edge;
            edge.x0 = a.x;
            edge.y0 = a.y;
            edge.x1 = b.x;
            edge.y1 = b.y;
            edge.yMin = std::min(a.y, b.y);
            edge.yMax = std::max(a.y, b.y);
            edges.push_back(edge);
        }
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

    constexpr int verticalSamples = 8;
    std::vector<double> coverage(static_cast<size_t>(cached.width) * cached.height, 0.0);
    std::vector<double> intersections;
    intersections.reserve(edges.size());

    for (int localY = 0; localY < cached.height; ++localY) {
        for (int sy = 0; sy < verticalSamples; ++sy) {
            const double sampleY = y0 + localY + (sy + 0.5) / verticalSamples;
            intersections.clear();

            for (const Edge& edge : edges) {
                if (sampleY >= edge.yMin && sampleY < edge.yMax) {
                    const double t = (sampleY - edge.y0) / (edge.y1 - edge.y0);
                    intersections.push_back(edge.x0 + t * (edge.x1 - edge.x0));
                }
            }

            if (intersections.size() < 2) continue;
            std::sort(intersections.begin(), intersections.end());

            for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
                AccumulateSpan(
                    coverage,
                    cached.width,
                    localY,
                    intersections[i] - x0,
                    intersections[i + 1] - x0,
                    verticalSamples);
            }
        }
    }

    for (size_t i = 0; i < coverage.size(); ++i) {
        const double alpha = std::min(1.0, std::max(0.0, coverage[i]));
        cached.alpha[i] = static_cast<uint8_t>(std::lround(alpha * 255.0));
    }

    auto inserted = glyphCache_.emplace(key, std::move(cached));
    return inserted.first->second;
}

} // namespace font
