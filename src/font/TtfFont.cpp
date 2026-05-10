#include "TtfFont.h"

#include <algorithm>
#include <fstream>

namespace font {

namespace {

constexpr uint16_t ARG_1_AND_2_ARE_WORDS = 0x0001;
constexpr uint16_t ARGS_ARE_XY_VALUES = 0x0002;
constexpr uint16_t WE_HAVE_A_SCALE = 0x0008;
constexpr uint16_t MORE_COMPONENTS = 0x0020;
constexpr uint16_t WE_HAVE_AN_X_AND_Y_SCALE = 0x0040;
constexpr uint16_t WE_HAVE_A_TWO_BY_TWO = 0x0080;

std::wstring AsciiToWide(const std::string& value)
{
    return std::wstring(value.begin(), value.end());
}

} // namespace

bool TtfFont::LoadFromFile(const std::wstring& path, std::wstring* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = L"Could not open font file.";
        return false;
    }

    data_.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (data_.size() < 12) {
        if (error) *error = L"Font file is too small.";
        return false;
    }

    tables_.clear();
    cmap_.clear();
    familyName_.clear();

    if (!ParseTables(error)) return false;
    if (!ParseRequiredTables(error)) return false;
    if (!ParseCmap(error)) return false;
    ParseName();
    return true;
}

bool TtfFont::ParseTables(std::wstring* error)
{
    const uint32_t sfntVersion = U32(0);
    if (sfntVersion != 0x00010000) {
        if (error) *error = L"Only TrueType outlines are supported in this first version.";
        return false;
    }

    const uint16_t numTables = U16(4);
    if (!HasBytes(12, static_cast<uint32_t>(numTables) * 16)) {
        if (error) *error = L"Invalid table directory.";
        return false;
    }

    for (uint16_t i = 0; i < numTables; ++i) {
        const uint32_t offset = 12 + i * 16;
        std::string tag(reinterpret_cast<const char*>(&data_[offset]), 4);
        Table table;
        table.offset = U32(offset + 8);
        table.length = U32(offset + 12);
        if (!HasBytes(table.offset, table.length)) {
            if (error) *error = L"Table extends beyond file size.";
            return false;
        }
        tables_[tag] = table;
    }

    return true;
}

bool TtfFont::ParseRequiredTables(std::wstring* error)
{
    const Table* head = FindTable("head");
    const Table* maxp = FindTable("maxp");
    const Table* hhea = FindTable("hhea");
    const Table* hmtx = FindTable("hmtx");
    const Table* loca = FindTable("loca");
    const Table* glyf = FindTable("glyf");
    if (!head || !maxp || !hhea || !hmtx || !loca || !glyf || !FindTable("cmap")) {
        if (error) *error = L"Missing a required TrueType table.";
        return false;
    }

    unitsPerEm_ = U16(head->offset + 18);
    indexToLocFormat_ = I16(head->offset + 50);
    numGlyphs_ = U16(maxp->offset + 4);
    ascender_ = I16(hhea->offset + 4);
    descender_ = I16(hhea->offset + 6);
    lineGap_ = I16(hhea->offset + 8);
    numHMetrics_ = U16(hhea->offset + 34);

    if (unitsPerEm_ == 0 || numGlyphs_ == 0 || (indexToLocFormat_ != 0 && indexToLocFormat_ != 1)) {
        if (error) *error = L"Unsupported or invalid font metrics.";
        return false;
    }
    return true;
}

bool TtfFont::ParseCmap(std::wstring* error)
{
    const Table* cmap = FindTable("cmap");
    const uint32_t base = cmap->offset;
    const uint16_t numTables = U16(base + 2);
    uint32_t best = 0;

    for (uint16_t i = 0; i < numTables; ++i) {
        const uint32_t rec = base + 4 + i * 8;
        const uint16_t platform = U16(rec);
        const uint16_t encoding = U16(rec + 2);
        const uint32_t subOffset = U32(rec + 4);
        const uint32_t subtable = base + subOffset;
        if (!HasBytes(subtable, 2)) continue;
        const uint16_t format = U16(subtable);
        if (format == 4 && platform == 3 && (encoding == 1 || encoding == 10)) {
            best = subtable;
            break;
        }
        if (format == 4 && best == 0) best = subtable;
    }

    if (best == 0) {
        if (error) *error = L"No cmap format 4 table found.";
        return false;
    }

    const uint16_t length = U16(best + 2);
    if (!HasBytes(best, length)) {
        if (error) *error = L"Invalid cmap format 4 table.";
        return false;
    }

    const uint16_t segCount = U16(best + 6) / 2;
    const uint32_t endCode = best + 14;
    const uint32_t startCode = endCode + segCount * 2 + 2;
    const uint32_t idDelta = startCode + segCount * 2;
    const uint32_t idRangeOffset = idDelta + segCount * 2;

    for (uint16_t seg = 0; seg < segCount; ++seg) {
        const uint16_t start = U16(startCode + seg * 2);
        const uint16_t end = U16(endCode + seg * 2);
        const int16_t delta = I16(idDelta + seg * 2);
        const uint16_t rangeOffset = U16(idRangeOffset + seg * 2);

        if (start == 0xFFFF && end == 0xFFFF) continue;
        for (uint32_t code = start; code <= end && code <= 0xFFFF; ++code) {
            uint16_t glyph = 0;
            if (rangeOffset == 0) {
                glyph = static_cast<uint16_t>((code + delta) & 0xFFFF);
            } else {
                const uint32_t glyphOffset = idRangeOffset + seg * 2 + rangeOffset + (code - start) * 2;
                if (HasBytes(glyphOffset, 2)) {
                    glyph = U16(glyphOffset);
                    if (glyph != 0) glyph = static_cast<uint16_t>((glyph + delta) & 0xFFFF);
                }
            }
            if (glyph != 0) cmap_[code] = glyph;
        }
    }
    return true;
}

bool TtfFont::ParseName()
{
    const Table* name = FindTable("name");
    if (!name || name->length < 6) return false;

    const uint32_t base = name->offset;
    const uint16_t count = U16(base + 2);
    const uint16_t stringOffset = U16(base + 4);

    for (uint16_t i = 0; i < count; ++i) {
        const uint32_t rec = base + 6 + i * 12;
        if (!HasBytes(rec, 12)) break;
        const uint16_t platform = U16(rec);
        const uint16_t nameId = U16(rec + 6);
        const uint16_t length = U16(rec + 8);
        const uint16_t offset = U16(rec + 10);
        if (nameId != 1) continue;

        const uint32_t text = base + stringOffset + offset;
        if (!HasBytes(text, length)) continue;

        if (platform == 3) {
            std::wstring value;
            for (uint16_t j = 0; j + 1 < length; j += 2) value.push_back(static_cast<wchar_t>(U16(text + j)));
            familyName_ = value;
            return true;
        }

        std::string value(reinterpret_cast<const char*>(&data_[text]), length);
        familyName_ = AsciiToWide(value);
        return true;
    }
    return false;
}

uint16_t TtfFont::GlyphIndexForCodepoint(uint32_t codepoint) const
{
    auto it = cmap_.find(codepoint);
    return it == cmap_.end() ? 0 : it->second;
}

bool TtfFont::LoadGlyph(uint16_t glyphIndex, GlyphOutline& outline, int depth) const
{
    if (glyphIndex >= numGlyphs_ || depth > 16) return false;

    outline = GlyphOutline();
    ApplyMetrics(glyphIndex, outline);

    const uint32_t glyphOffset = GlyphOffset(glyphIndex);
    const uint32_t glyphLength = GlyphLength(glyphIndex);
    if (glyphLength == 0) return true;
    if (!HasBytes(glyphOffset, glyphLength) || glyphLength < 10) return false;

    const int16_t contourCount = I16(glyphOffset);
    outline.xMin = I16(glyphOffset + 2);
    outline.yMin = I16(glyphOffset + 4);
    outline.xMax = I16(glyphOffset + 6);
    outline.yMax = I16(glyphOffset + 8);

    if (contourCount >= 0) return LoadSimpleGlyph(glyphIndex, glyphOffset, contourCount, outline);
    return LoadCompositeGlyph(glyphIndex, glyphOffset, outline, depth);
}

bool TtfFont::LoadSimpleGlyph(uint16_t, uint32_t glyphOffset, int16_t contourCount, GlyphOutline& outline) const
{
    if (contourCount == 0) return true;

    std::vector<uint16_t> endPts(contourCount);
    uint32_t p = glyphOffset + 10;
    for (int i = 0; i < contourCount; ++i) {
        endPts[i] = U16(p);
        p += 2;
    }

    const uint16_t instructionLength = U16(p);
    p += 2 + instructionLength;

    const uint16_t pointCount = static_cast<uint16_t>(endPts.back() + 1);
    std::vector<uint8_t> flags;
    flags.reserve(pointCount);
    while (flags.size() < pointCount) {
        const uint8_t flag = U8(p++);
        flags.push_back(flag);
        if (flag & 0x08) {
            const uint8_t repeat = U8(p++);
            for (uint8_t i = 0; i < repeat; ++i) flags.push_back(flag);
        }
    }

    std::vector<int16_t> xs(pointCount);
    std::vector<int16_t> ys(pointCount);
    int x = 0;
    for (uint16_t i = 0; i < pointCount; ++i) {
        const uint8_t f = flags[i];
        if (f & 0x02) {
            const int dx = U8(p++);
            x += (f & 0x10) ? dx : -dx;
        } else if ((f & 0x10) == 0) {
            x += I16(p);
            p += 2;
        }
        xs[i] = static_cast<int16_t>(x);
    }

    int y = 0;
    for (uint16_t i = 0; i < pointCount; ++i) {
        const uint8_t f = flags[i];
        if (f & 0x04) {
            const int dy = U8(p++);
            y += (f & 0x20) ? dy : -dy;
        } else if ((f & 0x20) == 0) {
            y += I16(p);
            p += 2;
        }
        ys[i] = static_cast<int16_t>(y);
    }

    uint16_t start = 0;
    for (int c = 0; c < contourCount; ++c) {
        const uint16_t end = endPts[c];
        const int count = end - start + 1;
        if (count <= 0) continue;

        struct RawPoint { Vec2 p; bool on; };
        std::vector<RawPoint> raw;
        raw.reserve(count);
        for (uint16_t i = start; i <= end; ++i) {
            raw.push_back({ Vec2{ static_cast<double>(xs[i]), static_cast<double>(ys[i]) }, (flags[i] & 0x01) != 0 });
        }

        Contour contour;
        auto midpoint = [](const Vec2& a, const Vec2& b) {
            return Vec2{ (a.x + b.x) * 0.5, (a.y + b.y) * 0.5 };
        };
        auto appendQuadratic = [&contour](const Vec2& p0, const Vec2& p1, const Vec2& p2) {
            constexpr int steps = 10;
            for (int step = 1; step <= steps; ++step) {
                const double t = static_cast<double>(step) / steps;
                const double mt = 1.0 - t;
                contour.points.push_back(Vec2{
                    mt * mt * p0.x + 2.0 * mt * t * p1.x + t * t * p2.x,
                    mt * mt * p0.y + 2.0 * mt * t * p1.y + t * t * p2.y
                });
            }
        };

        Vec2 current;
        int index = 0;
        if (raw[0].on) {
            current = raw[0].p;
            index = 1;
        } else if (raw[count - 1].on) {
            current = raw[count - 1].p;
            index = 0;
        } else {
            current = midpoint(raw[count - 1].p, raw[0].p);
            index = 0;
        }
        contour.points.push_back(current);

        for (int consumed = 0; consumed < count; ++consumed) {
            const RawPoint& next = raw[index % count];
            if (next.on) {
                contour.points.push_back(next.p);
                current = next.p;
                index++;
            } else {
                const RawPoint& after = raw[(index + 1) % count];
                const Vec2 endPoint = after.on ? after.p : midpoint(next.p, after.p);
                appendQuadratic(current, next.p, endPoint);
                current = endPoint;
                index += after.on ? 2 : 1;
                if (after.on) ++consumed;
            }
        }

        if (contour.points.size() >= 2) outline.contours.push_back(contour);
        start = static_cast<uint16_t>(end + 1);
    }

    return true;
}

bool TtfFont::LoadCompositeGlyph(uint16_t, uint32_t glyphOffset, GlyphOutline& outline, int depth) const
{
    uint32_t p = glyphOffset + 10;
    uint16_t flags = 0;
    do {
        flags = U16(p);
        const uint16_t componentGlyph = U16(p + 2);
        p += 4;

        int arg1 = 0;
        int arg2 = 0;
        if (flags & ARG_1_AND_2_ARE_WORDS) {
            arg1 = I16(p);
            arg2 = I16(p + 2);
            p += 4;
        } else {
            arg1 = I8(p);
            arg2 = I8(p + 1);
            p += 2;
        }

        ComponentTransform t;
        if (flags & ARGS_ARE_XY_VALUES) {
            t.e = static_cast<double>(arg1);
            t.f = static_cast<double>(arg2);
        }

        if (flags & WE_HAVE_A_SCALE) {
            const double s = I16(p) / 16384.0;
            p += 2;
            t.a = s;
            t.d = s;
        } else if (flags & WE_HAVE_AN_X_AND_Y_SCALE) {
            t.a = I16(p) / 16384.0;
            t.d = I16(p + 2) / 16384.0;
            p += 4;
        } else if (flags & WE_HAVE_A_TWO_BY_TWO) {
            t.a = I16(p) / 16384.0;
            t.b = I16(p + 2) / 16384.0;
            t.c = I16(p + 4) / 16384.0;
            t.d = I16(p + 6) / 16384.0;
            p += 8;
        }

        GlyphOutline child;
        if (!LoadGlyph(componentGlyph, child, depth + 1)) return false;
        for (const Contour& source : child.contours) {
            Contour dest;
            dest.points.reserve(source.points.size());
            for (const Vec2& point : source.points) {
                dest.points.push_back(Vec2{
                    t.a * point.x + t.c * point.y + t.e,
                    t.b * point.x + t.d * point.y + t.f
                });
            }
            outline.contours.push_back(dest);
        }
    } while (flags & MORE_COMPONENTS);

    return true;
}

bool TtfFont::HasBytes(uint32_t offset, uint32_t count) const
{
    return offset <= data_.size() && count <= data_.size() - offset;
}

uint8_t TtfFont::U8(uint32_t offset) const
{
    return data_[offset];
}

int8_t TtfFont::I8(uint32_t offset) const
{
    return static_cast<int8_t>(data_[offset]);
}

uint16_t TtfFont::U16(uint32_t offset) const
{
    return static_cast<uint16_t>((data_[offset] << 8) | data_[offset + 1]);
}

int16_t TtfFont::I16(uint32_t offset) const
{
    return static_cast<int16_t>(U16(offset));
}

uint32_t TtfFont::U32(uint32_t offset) const
{
    return (static_cast<uint32_t>(data_[offset]) << 24) |
        (static_cast<uint32_t>(data_[offset + 1]) << 16) |
        (static_cast<uint32_t>(data_[offset + 2]) << 8) |
        static_cast<uint32_t>(data_[offset + 3]);
}

const TtfFont::Table* TtfFont::FindTable(const char tag[5]) const
{
    auto it = tables_.find(std::string(tag, 4));
    return it == tables_.end() ? nullptr : &it->second;
}

uint32_t TtfFont::GlyphOffset(uint16_t glyphIndex) const
{
    const Table* loca = FindTable("loca");
    const Table* glyf = FindTable("glyf");
    if (indexToLocFormat_ == 0) return glyf->offset + U16(loca->offset + glyphIndex * 2) * 2;
    return glyf->offset + U32(loca->offset + glyphIndex * 4);
}

uint32_t TtfFont::GlyphLength(uint16_t glyphIndex) const
{
    const uint32_t a = GlyphOffset(glyphIndex);
    const uint32_t b = GlyphOffset(static_cast<uint16_t>(glyphIndex + 1));
    return b >= a ? b - a : 0;
}

void TtfFont::ApplyMetrics(uint16_t glyphIndex, GlyphOutline& outline) const
{
    const Table* hmtx = FindTable("hmtx");
    if (glyphIndex < numHMetrics_) {
        const uint32_t p = hmtx->offset + glyphIndex * 4;
        outline.advanceWidth = U16(p);
        outline.leftSideBearing = I16(p + 2);
        return;
    }

    const uint32_t lastMetric = hmtx->offset + (numHMetrics_ - 1) * 4;
    outline.advanceWidth = U16(lastMetric);
    const uint32_t lsbOffset = hmtx->offset + numHMetrics_ * 4 + (glyphIndex - numHMetrics_) * 2;
    outline.leftSideBearing = I16(lsbOffset);
}

} // namespace font
