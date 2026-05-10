#include "TtfFont.h"

#include <algorithm>
#include <fstream>
#include <numeric>

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
    ligaturesByFirst_.clear();
    kernPairs_.clear();
    gposPairs_.clear();
    familyName_.clear();

    if (!ParseTables(error)) return false;
    if (!ParseRequiredTables(error)) return false;
    if (!ParseCmap(error)) return false;
    ParseName();
    ParseKern();
    ParseGsub();
    ParseGpos();
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

        if (platform == 0 || platform == 3) {
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

std::vector<PositionedGlyph> TtfFont::ShapeText(const std::wstring& text) const
{
    std::vector<uint16_t> input;
    input.reserve(text.size());
    for (wchar_t ch : text) input.push_back(GlyphIndexForCodepoint(static_cast<uint32_t>(ch)));

    std::vector<uint16_t> glyphs;
    for (size_t i = 0; i < input.size();) {
        const uint16_t first = input[i];
        const auto found = ligaturesByFirst_.find(first);
        const LigatureSubstitution* best = nullptr;
        if (found != ligaturesByFirst_.end()) {
            for (const LigatureSubstitution& ligature : found->second) {
                if (i + ligature.components.size() > input.size()) continue;
                bool match = true;
                for (size_t j = 0; j < ligature.components.size(); ++j) {
                    if (input[i + j] != ligature.components[j]) {
                        match = false;
                        break;
                    }
                }
                if (match && (!best || ligature.components.size() > best->components.size())) best = &ligature;
            }
        }

        if (best) {
            glyphs.push_back(best->ligatureGlyph);
            i += best->components.size();
        } else {
            glyphs.push_back(first);
            ++i;
        }
    }

    std::vector<PositionedGlyph> shaped;
    shaped.reserve(glyphs.size());
    for (uint16_t glyph : glyphs) {
        PositionedGlyph positioned;
        positioned.glyphIndex = glyph;
        positioned.xAdvance = static_cast<int16_t>(GlyphAdvanceWidth(glyph));
        shaped.push_back(positioned);
    }

    for (size_t i = 0; i + 1 < shaped.size(); ++i) {
        shaped[i].xAdvance = static_cast<int16_t>(shaped[i].xAdvance + PairAdjustment(shaped[i].glyphIndex, shaped[i + 1].glyphIndex));
    }

    return shaped;
}

void TtfFont::ParseKern()
{
    const Table* kern = FindTable("kern");
    if (!kern || kern->length < 4) return;

    const uint32_t base = kern->offset;
    const uint16_t nTables = U16(base + 2);
    uint32_t subtable = base + 4;
    for (uint16_t i = 0; i < nTables && HasBytes(subtable, 6); ++i) {
        const uint16_t length = U16(subtable + 2);
        const uint16_t coverage = U16(subtable + 4);
        const uint16_t format = coverage >> 8;
        if (length < 6 || !HasBytes(subtable, length)) break;

        if (format == 0 && length >= 14) {
            const uint16_t nPairs = U16(subtable + 6);
            uint32_t p = subtable + 14;
            for (uint16_t pair = 0; pair < nPairs && HasBytes(p, 6); ++pair) {
                const uint16_t left = U16(p);
                const uint16_t right = U16(p + 2);
                const int16_t value = I16(p + 4);
                kernPairs_[(static_cast<uint32_t>(left) << 16) | right] = value;
                p += 6;
            }
        }
        subtable += length;
    }
}

void TtfFont::ParseGsub()
{
    const Table* gsub = FindTable("GSUB");
    if (!gsub || gsub->length < 10) return;

    const uint32_t base = gsub->offset;
    const uint16_t lookupListOffset = U16(base + 8);
    const uint32_t lookupList = base + lookupListOffset;
    if (!HasBytes(lookupList, 2)) return;

    const uint16_t lookupCount = U16(lookupList);
    for (uint16_t i = 0; i < lookupCount; ++i) {
        const uint32_t lookupOffsetPos = lookupList + 2 + i * 2;
        if (!HasBytes(lookupOffsetPos, 2)) return;
        const uint32_t lookup = lookupList + U16(lookupOffsetPos);
        if (!HasBytes(lookup, 6)) continue;

        const uint16_t subtableCount = U16(lookup + 4);
        const uint16_t lookupType = U16(lookup);
        for (uint16_t sub = 0; sub < subtableCount; ++sub) {
            const uint32_t subOffsetPos = lookup + 6 + sub * 2;
            if (!HasBytes(subOffsetPos, 2)) break;
            const uint32_t subtable = lookup + U16(subOffsetPos);
            if (lookupType == 4) {
                ParseGsubLigatureSubtable(subtable);
            } else if (lookupType == 7 && HasBytes(subtable, 8) && U16(subtable) == 1) {
                const uint16_t extensionLookupType = U16(subtable + 2);
                const uint32_t extensionOffset = U32(subtable + 4);
                if (extensionLookupType == 4) ParseGsubLigatureSubtable(subtable + extensionOffset);
            }
        }
    }

    for (auto& item : ligaturesByFirst_) {
        std::sort(item.second.begin(), item.second.end(), [](const LigatureSubstitution& a, const LigatureSubstitution& b) {
            return a.components.size() > b.components.size();
        });
    }
}

void TtfFont::ParseGsubLigatureSubtable(uint32_t subtable)
{
    if (!HasBytes(subtable, 6) || U16(subtable) != 1) return;

    const std::vector<uint16_t> coverage = ReadCoverage(subtable + U16(subtable + 2));
    const uint16_t ligatureSetCount = U16(subtable + 4);
    const uint16_t count = std::min<uint16_t>(ligatureSetCount, static_cast<uint16_t>(coverage.size()));

    for (uint16_t i = 0; i < count; ++i) {
        const uint32_t setOffsetPos = subtable + 6 + i * 2;
        if (!HasBytes(setOffsetPos, 2)) break;
        const uint32_t set = subtable + U16(setOffsetPos);
        if (!HasBytes(set, 2)) continue;

        const uint16_t ligatureCount = U16(set);
        for (uint16_t lig = 0; lig < ligatureCount; ++lig) {
            const uint32_t ligOffsetPos = set + 2 + lig * 2;
            if (!HasBytes(ligOffsetPos, 2)) break;
            const uint32_t record = set + U16(ligOffsetPos);
            if (!HasBytes(record, 4)) continue;

            const uint16_t ligatureGlyph = U16(record);
            const uint16_t componentCount = U16(record + 2);
            if (ligatureGlyph == 0) continue;
            if (componentCount < 2 || !HasBytes(record + 4, static_cast<uint32_t>(componentCount - 1) * 2)) continue;

            LigatureSubstitution subst;
            subst.ligatureGlyph = ligatureGlyph;
            subst.components.push_back(coverage[i]);
            for (uint16_t component = 1; component < componentCount; ++component) {
                subst.components.push_back(U16(record + 4 + (component - 1) * 2));
            }
            ligaturesByFirst_[coverage[i]].push_back(std::move(subst));
        }
    }
}

void TtfFont::ParseGpos()
{
    const Table* gpos = FindTable("GPOS");
    if (!gpos || gpos->length < 10) return;

    const uint32_t base = gpos->offset;
    const uint16_t lookupListOffset = U16(base + 8);
    const uint32_t lookupList = base + lookupListOffset;
    if (!HasBytes(lookupList, 2)) return;

    const uint16_t lookupCount = U16(lookupList);
    for (uint16_t i = 0; i < lookupCount; ++i) {
        const uint32_t lookupOffsetPos = lookupList + 2 + i * 2;
        if (!HasBytes(lookupOffsetPos, 2)) return;
        const uint32_t lookup = lookupList + U16(lookupOffsetPos);
        if (!HasBytes(lookup, 6)) continue;

        const uint16_t subtableCount = U16(lookup + 4);
        const uint16_t lookupType = U16(lookup);
        for (uint16_t sub = 0; sub < subtableCount; ++sub) {
            const uint32_t subOffsetPos = lookup + 6 + sub * 2;
            if (!HasBytes(subOffsetPos, 2)) break;
            const uint32_t subtable = lookup + U16(subOffsetPos);
            if (lookupType == 2) {
                ParseGposPairSubtable(subtable);
            } else if (lookupType == 9 && HasBytes(subtable, 8) && U16(subtable) == 1) {
                const uint16_t extensionLookupType = U16(subtable + 2);
                const uint32_t extensionOffset = U32(subtable + 4);
                if (extensionLookupType == 2) ParseGposPairSubtable(subtable + extensionOffset);
            }
        }
    }
}

void TtfFont::ParseGposPairSubtable(uint32_t subtable)
{
    if (!HasBytes(subtable, 10)) return;

    const uint16_t posFormat = U16(subtable);
    const std::vector<uint16_t> coverage = ReadCoverage(subtable + U16(subtable + 2));
    const uint16_t valueFormat1 = U16(subtable + 4);
    const uint16_t valueFormat2 = U16(subtable + 6);
    const uint32_t valueSize1 = ValueRecordSize(valueFormat1);
    const uint32_t valueSize2 = ValueRecordSize(valueFormat2);

    if (posFormat == 1) {
        const uint16_t pairSetCount = U16(subtable + 8);
        const uint16_t count = std::min<uint16_t>(pairSetCount, static_cast<uint16_t>(coverage.size()));
        for (uint16_t i = 0; i < count; ++i) {
            const uint32_t pairSetOffsetPos = subtable + 10 + i * 2;
            if (!HasBytes(pairSetOffsetPos, 2)) break;
            const uint32_t pairSet = subtable + U16(pairSetOffsetPos);
            if (!HasBytes(pairSet, 2)) continue;

            const uint16_t pairValueCount = U16(pairSet);
            uint32_t p = pairSet + 2;
            for (uint16_t pair = 0; pair < pairValueCount && HasBytes(p, 2 + valueSize1 + valueSize2); ++pair) {
                const uint16_t second = U16(p);
                const int16_t adjust = ReadXAdvanceFromValueRecord(p + 2, valueFormat1);
                if (adjust != 0) gposPairs_[(static_cast<uint32_t>(coverage[i]) << 16) | second] = adjust;
                p += 2 + valueSize1 + valueSize2;
            }
        }
    } else if (posFormat == 2 && HasBytes(subtable, 16)) {
        const std::vector<uint16_t> classDef1 = ReadClassDef(subtable + U16(subtable + 8));
        const std::vector<uint16_t> classDef2 = ReadClassDef(subtable + U16(subtable + 10));
        const uint16_t class1Count = U16(subtable + 12);
        const uint16_t class2Count = U16(subtable + 14);
        const uint32_t recordSize = valueSize1 + valueSize2;
        const uint32_t records = subtable + 16;
        if (recordSize == 0 || !HasBytes(records, static_cast<uint32_t>(class1Count) * class2Count * recordSize)) return;

        std::vector<std::vector<uint16_t>> glyphsByClass2(class2Count);
        for (uint16_t glyph = 0; glyph < numGlyphs_; ++glyph) {
            const uint16_t cls = glyph < classDef2.size() ? classDef2[glyph] : 0;
            if (cls < class2Count) glyphsByClass2[cls].push_back(glyph);
        }

        for (uint16_t first : coverage) {
            const uint16_t class1 = first < classDef1.size() ? classDef1[first] : 0;
            if (class1 >= class1Count) continue;
            for (uint16_t class2 = 0; class2 < class2Count; ++class2) {
                const uint32_t record = records + (static_cast<uint32_t>(class1) * class2Count + class2) * recordSize;
                const int16_t adjust = ReadXAdvanceFromValueRecord(record, valueFormat1);
                if (adjust == 0) continue;
                for (uint16_t second : glyphsByClass2[class2]) {
                    gposPairs_[(static_cast<uint32_t>(first) << 16) | second] = adjust;
                }
            }
        }
    }
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

std::vector<uint16_t> TtfFont::ReadCoverage(uint32_t offset) const
{
    std::vector<uint16_t> glyphs;
    if (!HasBytes(offset, 4)) return glyphs;

    const uint16_t format = U16(offset);
    if (format == 1) {
        const uint16_t count = U16(offset + 2);
        if (!HasBytes(offset + 4, static_cast<uint32_t>(count) * 2)) return glyphs;
        glyphs.reserve(count);
        for (uint16_t i = 0; i < count; ++i) glyphs.push_back(U16(offset + 4 + i * 2));
    } else if (format == 2) {
        const uint16_t rangeCount = U16(offset + 2);
        if (!HasBytes(offset + 4, static_cast<uint32_t>(rangeCount) * 6)) return glyphs;
        for (uint16_t i = 0; i < rangeCount; ++i) {
            const uint32_t record = offset + 4 + i * 6;
            const uint16_t start = U16(record);
            const uint16_t end = U16(record + 2);
            for (uint32_t glyph = start; glyph <= end && glyph <= 0xFFFF; ++glyph) {
                glyphs.push_back(static_cast<uint16_t>(glyph));
            }
        }
    }
    return glyphs;
}

std::vector<uint16_t> TtfFont::ReadClassDef(uint32_t offset) const
{
    std::vector<uint16_t> classes(numGlyphs_, 0);
    if (!HasBytes(offset, 4)) return classes;

    const uint16_t format = U16(offset);
    if (format == 1) {
        const uint16_t startGlyph = U16(offset + 2);
        const uint16_t glyphCount = U16(offset + 4);
        if (!HasBytes(offset + 6, static_cast<uint32_t>(glyphCount) * 2)) return classes;
        for (uint16_t i = 0; i < glyphCount; ++i) {
            const uint32_t glyph = startGlyph + i;
            if (glyph < classes.size()) classes[glyph] = U16(offset + 6 + i * 2);
        }
    } else if (format == 2) {
        const uint16_t rangeCount = U16(offset + 2);
        if (!HasBytes(offset + 4, static_cast<uint32_t>(rangeCount) * 6)) return classes;
        for (uint16_t i = 0; i < rangeCount; ++i) {
            const uint32_t record = offset + 4 + i * 6;
            const uint16_t start = U16(record);
            const uint16_t end = U16(record + 2);
            const uint16_t cls = U16(record + 4);
            for (uint32_t glyph = start; glyph <= end && glyph < classes.size(); ++glyph) classes[glyph] = cls;
        }
    }
    return classes;
}

uint32_t TtfFont::ValueRecordSize(uint16_t valueFormat) const
{
    uint32_t size = 0;
    for (uint16_t bit = 0; bit < 8; ++bit) {
        if (valueFormat & (1u << bit)) size += 2;
    }
    return size;
}

int16_t TtfFont::ReadXAdvanceFromValueRecord(uint32_t offset, uint16_t valueFormat) const
{
    uint32_t p = offset;
    if (valueFormat & 0x0001) p += 2; // xPlacement
    if (valueFormat & 0x0002) p += 2; // yPlacement
    if (valueFormat & 0x0004) return I16(p);
    return 0;
}

uint16_t TtfFont::GlyphAdvanceWidth(uint16_t glyphIndex) const
{
    const Table* hmtx = FindTable("hmtx");
    if (!hmtx || numHMetrics_ == 0) return 0;
    if (glyphIndex < numHMetrics_) return U16(hmtx->offset + glyphIndex * 4);
    return U16(hmtx->offset + (numHMetrics_ - 1) * 4);
}

int16_t TtfFont::PairAdjustment(uint16_t leftGlyph, uint16_t rightGlyph) const
{
    const uint32_t key = (static_cast<uint32_t>(leftGlyph) << 16) | rightGlyph;
    auto gpos = gposPairs_.find(key);
    if (gpos != gposPairs_.end()) return gpos->second;
    auto kern = kernPairs_.find(key);
    return kern != kernPairs_.end() ? kern->second : 0;
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
