#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace font {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Contour {
    std::vector<Vec2> points;
};

struct GlyphOutline {
    int16_t xMin = 0;
    int16_t yMin = 0;
    int16_t xMax = 0;
    int16_t yMax = 0;
    uint16_t advanceWidth = 0;
    int16_t leftSideBearing = 0;
    std::vector<Contour> contours;
};

struct PositionedGlyph {
    uint16_t glyphIndex = 0;
    int16_t xOffset = 0;
    int16_t yOffset = 0;
    int16_t xAdvance = 0;
};

class TtfFont {
public:
    bool LoadFromFile(const std::wstring& path, std::wstring* error = nullptr);

    uint16_t GlyphIndexForCodepoint(uint32_t codepoint) const;
    bool LoadGlyph(uint16_t glyphIndex, GlyphOutline& outline, int depth = 0) const;
    std::vector<PositionedGlyph> ShapeText(const std::wstring& text) const;

    uint16_t UnitsPerEm() const { return unitsPerEm_; }
    int16_t Ascender() const { return ascender_; }
    int16_t Descender() const { return descender_; }
    int16_t LineGap() const { return lineGap_; }
    uint16_t NumGlyphs() const { return numGlyphs_; }
    const std::wstring& FamilyName() const { return familyName_; }

private:
    struct Table {
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    struct ComponentTransform {
        double a = 1.0;
        double b = 0.0;
        double c = 0.0;
        double d = 1.0;
        double e = 0.0;
        double f = 0.0;
    };

    struct LigatureSubstitution {
        std::vector<uint16_t> components;
        uint16_t ligatureGlyph = 0;
    };

    bool ParseTables(std::wstring* error);
    bool ParseRequiredTables(std::wstring* error);
    bool ParseCmap(std::wstring* error);
    bool ParseName();
    void ParseKern();
    void ParseGsub();
    void ParseGpos();
    void ParseGsubLigatureSubtable(uint32_t subtable);
    void ParseGposPairSubtable(uint32_t subtable);

    bool HasBytes(uint32_t offset, uint32_t count) const;
    uint8_t U8(uint32_t offset) const;
    int8_t I8(uint32_t offset) const;
    uint16_t U16(uint32_t offset) const;
    int16_t I16(uint32_t offset) const;
    uint32_t U32(uint32_t offset) const;

    const Table* FindTable(const char tag[5]) const;
    std::vector<uint16_t> ReadCoverage(uint32_t offset) const;
    std::vector<uint16_t> ReadClassDef(uint32_t offset) const;
    uint32_t ValueRecordSize(uint16_t valueFormat) const;
    int16_t ReadXAdvanceFromValueRecord(uint32_t offset, uint16_t valueFormat) const;
    uint16_t GlyphAdvanceWidth(uint16_t glyphIndex) const;
    int16_t PairAdjustment(uint16_t leftGlyph, uint16_t rightGlyph) const;
    uint32_t GlyphOffset(uint16_t glyphIndex) const;
    uint32_t GlyphLength(uint16_t glyphIndex) const;
    void ApplyMetrics(uint16_t glyphIndex, GlyphOutline& outline) const;
    bool LoadSimpleGlyph(uint16_t glyphIndex, uint32_t glyphOffset, int16_t contourCount, GlyphOutline& outline) const;
    bool LoadCompositeGlyph(uint16_t glyphIndex, uint32_t glyphOffset, GlyphOutline& outline, int depth) const;

    std::vector<uint8_t> data_;
    std::unordered_map<std::string, Table> tables_;
    std::unordered_map<uint32_t, uint16_t> cmap_;
    std::unordered_map<uint16_t, std::vector<LigatureSubstitution>> ligaturesByFirst_;
    std::unordered_map<uint32_t, int16_t> kernPairs_;
    std::unordered_map<uint32_t, int16_t> gposPairs_;
    std::wstring familyName_;

    uint16_t unitsPerEm_ = 0;
    uint16_t numGlyphs_ = 0;
    uint16_t numHMetrics_ = 0;
    int16_t ascender_ = 0;
    int16_t descender_ = 0;
    int16_t lineGap_ = 0;
    int16_t indexToLocFormat_ = 0;
};

} // namespace font
