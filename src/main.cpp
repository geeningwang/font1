#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shobjidl.h>

#include "font/FontRenderer.h"
#include "font/TtfFont.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"Font1WindowClass";
constexpr wchar_t kFontPath[] = L"C:\\Windows\\Fonts\\times.ttf";
constexpr wchar_t kDefaultFontDirectory[] = L"C:\\Windows\\Fonts";
constexpr uint32_t kBlankBackground = 0xFFFFFFFF;
constexpr int kSidebarWidth = 280;
constexpr int kFontCategoryId = 1999;
constexpr int kFontDirectoryLabelId = 2000;
constexpr int kFontListId = 2001;
constexpr int kFontPathId = 2002;
constexpr int kFontOpenId = 2003;

enum class FontCategory {
    Ttf,
    Ttc,
    Otf,
    Fon,
    Fnt
};

struct FontFileItem {
    std::wstring name;
    std::wstring path;
};

font::TtfFont g_font;
std::unique_ptr<font::FontRenderer> g_renderer;
font::Image g_image;
std::wstring g_status;
std::wstring g_fontPath;
std::wstring g_fontDirectory = kDefaultFontDirectory;
HWND g_fontCategoryCombo = nullptr;
HWND g_fontDirectoryLabel = nullptr;
HWND g_fontList = nullptr;
HWND g_fontPathEdit = nullptr;
HWND g_fontOpenButton = nullptr;
std::vector<FontFileItem> g_fontFiles;
FontCategory g_fontCategory = FontCategory::Ttf;
bool g_inSizeMove = false;
bool g_trackingMouse = false;

struct TooltipInfo {
    bool visible = false;
    int x = 0;
    int y = 0;
    std::wstring name;
    std::wstring meaning;
};

TooltipInfo g_tooltip;

constexpr UINT_PTR kResizeTimer = 1;
constexpr UINT kResizeDebounceMs = 120;

void DrawRect(font::Image& image, int x0, int y0, int x1, int y1, uint32_t color)
{
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(image.width, x1);
    y1 = std::min(image.height, y1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            image.pixels[static_cast<size_t>(y) * image.width + x] = color;
        }
    }
}

void DrawLine(font::Image& image, double x0, double y0, double x1, double y1, uint32_t color)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dy)))));
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const int x = static_cast<int>(std::lround(x0 + dx * t));
        const int y = static_cast<int>(std::lround(y0 + dy * t));
        image.BlendPixel(x, y, 220, color);
    }
}

void DrawRectOutline(font::Image& image, double x0, double y0, double x1, double y1, uint32_t color)
{
    DrawLine(image, x0, y0, x1, y0, color);
    DrawLine(image, x1, y0, x1, y1, color);
    DrawLine(image, x1, y1, x0, y1, color);
    DrawLine(image, x0, y1, x0, y0, color);
}

double DistanceToSegment(double px, double py, double x0, double y0, double x1, double y1)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double lengthSq = dx * dx + dy * dy;
    if (lengthSq <= 0.0001) return std::hypot(px - x0, py - y0);

    const double t = std::max(0.0, std::min(1.0, ((px - x0) * dx + (py - y0) * dy) / lengthSq));
    const double cx = x0 + t * dx;
    const double cy = y0 + t * dy;
    return std::hypot(px - cx, py - cy);
}

struct InspectorLayout {
    bool valid = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    uint16_t glyphIndex = 0;
    font::GlyphOutline outline;
    double scale = 1.0;
    double pixelSize = 1.0;
    double originX = 0.0;
    double baselineY = 0.0;
    double lineLeft = 0.0;
    double lineRight = 0.0;

    double SX(double fontX) const { return originX + fontX * scale; }
    double SY(double fontY) const { return baselineY - fontY * scale; }
};

struct AuxLine {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    uint32_t color = 0xFFFF0000;
    std::wstring name;
    std::wstring meaning;
};

InspectorLayout BuildInspectorLayout(int left, int top, int right, int bottom)
{
    InspectorLayout layout;
    if (right - left < 160 || bottom - top < 220 || !g_renderer) return layout;

    layout.left = left;
    layout.top = top;
    layout.right = right;
    layout.bottom = bottom;
    layout.glyphIndex = g_font.GlyphIndexForCodepoint(L'A');
    if (!g_font.LoadGlyph(layout.glyphIndex, layout.outline)) return layout;

    const int panelWidth = right - left;
    const int panelHeight = bottom - top;
    const double designLeft = std::min(0.0, static_cast<double>(layout.outline.xMin));
    const double designRight = std::max(static_cast<double>(g_font.UnitsPerEm()), static_cast<double>(layout.outline.advanceWidth));
    const double scaleByWidth = (panelWidth - 64.0) / std::max(1.0, designRight - designLeft);
    const double scaleByHeight = (panelHeight - 70.0) /
        std::max(1.0, static_cast<double>(g_font.Ascender() - g_font.Descender()));
    layout.scale = std::max(0.02, std::min(scaleByWidth, scaleByHeight));
    layout.pixelSize = layout.scale * g_font.UnitsPerEm();
    layout.originX = left + 32.0 - layout.outline.leftSideBearing * layout.scale;
    layout.baselineY = top + 42.0 + g_font.Ascender() * layout.scale;
    layout.lineLeft = left + 12.0;
    layout.lineRight = right - 12.0;
    layout.valid = true;
    return layout;
}

void AddRectLines(std::vector<AuxLine>& lines, double x0, double y0, double x1, double y1, uint32_t color, const std::wstring& name, const std::wstring& meaning)
{
    lines.push_back({ x0, y0, x1, y0, color, name, meaning });
    lines.push_back({ x1, y0, x1, y1, color, name, meaning });
    lines.push_back({ x1, y1, x0, y1, color, name, meaning });
    lines.push_back({ x0, y1, x0, y0, color, name, meaning });
}

std::vector<AuxLine> BuildAuxLines(const InspectorLayout& l)
{
    std::vector<AuxLine> lines;
    if (!l.valid) return lines;

    constexpr uint32_t red = 0xFFFF0000;
    constexpr uint32_t blue = 0xFF2563EB;

    auto horizontal = [&](double y, const wchar_t* name, const wchar_t* meaning) {
        lines.push_back({ l.lineLeft, y, l.lineRight, y, red, name, meaning });
    };
    auto vertical = [&](double x, const wchar_t* name, const wchar_t* meaning) {
        lines.push_back({ x, l.top + 12.0, x, l.bottom - 12.0, red, name, meaning });
    };

    horizontal(l.SY(0.0), L"Baseline", L"Glyphs sit on this y=0 writing line.");
    horizontal(l.SY(g_font.Ascender()), L"Ascender", L"Recommended top of normal line spacing.");
    horizontal(l.SY(g_font.Descender()), L"Descender", L"Recommended bottom for descenders.");
    horizontal(l.SY(g_font.UnitsPerEm()), L"Em top", L"Top of the 2048-unit design square.");
    horizontal(l.SY(g_font.Ascender() + g_font.LineGap()), L"Line gap top", L"Extra vertical gap above the ascender.");
    horizontal(l.SY(l.outline.yMax), L"Glyph bbox top", L"Highest point in the A outline.");
    horizontal(l.SY(l.outline.yMin), L"Glyph bbox bottom", L"Lowest point in the A outline.");

    vertical(l.SX(0.0), L"Glyph origin", L"Pen position before drawing this glyph.");
    vertical(l.SX(l.outline.leftSideBearing), L"Left side bearing", L"Space from origin to the black shape.");
    vertical(l.SX(l.outline.xMin), L"Glyph bbox left", L"Left edge of the outline bounding box.");
    vertical(l.SX(l.outline.xMax), L"Glyph bbox right", L"Right edge of the outline bounding box.");
    vertical(l.SX(l.outline.advanceWidth), L"Advance width", L"Pen moves here before the next glyph.");

    AddRectLines(lines, l.SX(l.outline.xMin), l.SY(l.outline.yMax), l.SX(l.outline.xMax), l.SY(l.outline.yMin), red,
        L"Glyph bounding box", L"Tight rectangle around the A outline.");
    AddRectLines(lines, l.SX(0.0), l.SY(g_font.UnitsPerEm()), l.SX(g_font.UnitsPerEm()), l.SY(0.0), blue,
        L"Em square", L"Design coordinate square scaled to pixels.");

    for (const font::Contour& contour : l.outline.contours) {
        if (contour.points.size() < 2) continue;
        for (size_t i = 0, j = contour.points.size() - 1; i < contour.points.size(); j = i++) {
            lines.push_back({
                l.SX(contour.points[j].x),
                l.SY(contour.points[j].y),
                l.SX(contour.points[i].x),
                l.SY(contour.points[i].y),
                red,
                L"Outline segment",
                L"Flattened TrueType quadratic contour edge."
            });
        }
    }

    return lines;
}

bool PointInsideGlyph(const InspectorLayout& l, double x, double y)
{
    bool inside = false;
    for (const font::Contour& contour : l.outline.contours) {
        if (contour.points.size() < 2) continue;
        for (size_t i = 0, j = contour.points.size() - 1; i < contour.points.size(); j = i++) {
            const double x0 = l.SX(contour.points[j].x);
            const double y0 = l.SY(contour.points[j].y);
            const double x1 = l.SX(contour.points[i].x);
            const double y1 = l.SY(contour.points[i].y);
            if ((y0 > y) != (y1 > y)) {
                const double atX = (x1 - x0) * (y - y0) / (y1 - y0) + x0;
                if (x < atX) inside = !inside;
            }
        }
    }
    return inside;
}

void DrawGlyphGrid(font::Image& image, const font::FontRenderer& renderer, int left, int top)
{
    constexpr int cellW = 48;
    constexpr int cellH = 54;
    constexpr int cols = 16;
    const wchar_t chars[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    for (int i = 0; chars[i] != 0; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const int x = left + col * cellW;
        const int y = top + row * cellH;
        DrawRect(image, x, y, x + cellW - 2, y + cellH - 2, 0xFFF4F1EA);
        const uint16_t glyph = g_font.GlyphIndexForCodepoint(chars[i]);
        renderer.DrawGlyph(image, glyph, x + 10.0, y + 39.0, 32.0, 0xFF1F2630);
    }
}

void DrawLargeAInspector(font::Image& image, int left, int top, int right, int bottom)
{
    constexpr uint32_t ink = 0xFF111827;
    constexpr uint32_t panel = 0xFFFCFBF7;

    const InspectorLayout layout = BuildInspectorLayout(left, top, right, bottom);
    if (!layout.valid) return;

    DrawRect(image, left, top, right, bottom, panel);

    const std::vector<AuxLine> lines = BuildAuxLines(layout);
    for (const AuxLine& line : lines) {
        DrawLine(image, line.x0, line.y0, line.x1, line.y1, line.color);
    }

    g_renderer->DrawGlyph(image, layout.glyphIndex, layout.originX, layout.baselineY, layout.pixelSize, ink);

    const uint16_t glyphB = g_font.GlyphIndexForCodepoint(L'B');
    g_renderer->DrawGlyph(image, glyphB, layout.originX + layout.outline.advanceWidth * layout.scale, layout.baselineY, layout.pixelSize, 0xFF9CA3AF);
    const double secondRowGap = (g_font.Ascender() - g_font.Descender() + g_font.LineGap()) * layout.scale;
    g_renderer->DrawString(image, L"fine", layout.originX, layout.baselineY + secondRowGap, layout.pixelSize, 0xFF9CA3AF);

    for (const AuxLine& line : lines) {
        DrawLine(image, line.x0, line.y0, line.x1, line.y1, line.color);
    }
}

void DrawTooltip(font::Image& image)
{
    if (!g_tooltip.visible || !g_renderer) return;

    constexpr int tooltipW = 350;
    constexpr int tooltipH = 86;
    int x = g_tooltip.x + 18;
    int y = g_tooltip.y + 18;
    if (x + tooltipW >= image.width) x = g_tooltip.x - tooltipW - 18;
    if (y + tooltipH >= image.height) y = g_tooltip.y - tooltipH - 18;
    x = std::max(8, std::min(x, image.width - tooltipW - 8));
    y = std::max(8, std::min(y, image.height - tooltipH - 8));

    DrawRect(image, x + 3, y + 3, x + tooltipW + 3, y + tooltipH + 3, 0x55333333);
    DrawRect(image, x, y, x + tooltipW, y + tooltipH, 0xFFFFFDF8);
    DrawRectOutline(image, x, y, x + tooltipW, y + tooltipH, 0xFFB91C1C);
    g_renderer->DrawString(image, g_tooltip.name, x + 14.0, y + 31.0, 21.0, 0xFF111827);
    g_renderer->DrawString(image, g_tooltip.meaning, x + 14.0, y + 63.0, 16.0, 0xFF4B5563);
}

TooltipInfo HitTestInspector(int mouseX, int mouseY, int width, int height)
{
    TooltipInfo info;
    info.x = mouseX;
    info.y = mouseY;

    const int inspectorLeft = std::max(650, width - 430);
    const InspectorLayout layout = BuildInspectorLayout(inspectorLeft, 24, width - 24, height - 24);
    if (!layout.valid) return info;

    constexpr double hitRadius = 5.0;
    const std::vector<AuxLine> lines = BuildAuxLines(layout);
    for (const AuxLine& line : lines) {
        if (DistanceToSegment(mouseX, mouseY, line.x0, line.y0, line.x1, line.y1) <= hitRadius) {
            info.visible = true;
            info.name = line.name;
            info.meaning = line.meaning;
            return info;
        }
    }

    if (mouseX < layout.left || mouseX >= layout.right || mouseY < layout.top || mouseY >= layout.bottom) return info;

    if (PointInsideGlyph(layout, mouseX, mouseY)) {
        info.visible = true;
        info.name = L"Glyph A";
        info.meaning = L"Filled shape rasterized from TrueType outlines.";
    }
    return info;
}

bool SameTooltip(const TooltipInfo& a, const TooltipInfo& b)
{
    return a.visible == b.visible && a.name == b.name && a.meaning == b.meaning &&
        std::abs(a.x - b.x) < 2 && std::abs(a.y - b.y) < 2;
}

void RenderContent(int width, int height)
{
    g_image.Resize(width, height);
    g_image.Clear(0xFFE7E2D8);

    DrawRect(g_image, 24, 24, width - 24, 158, 0xFFF8F7F2);
    const std::wstring familyName = g_font.FamilyName().empty() ? L"Selected Font" : g_font.FamilyName();
    g_renderer->DrawString(g_image, familyName, 48.0, 91.0, 54.0, 0xFF111827);
    g_renderer->DrawString(g_image, L"Custom TrueType parser + software rasterizer", 50.0, 132.0, 20.0, 0xFF46515F);

    g_renderer->DrawString(g_image, L"The quick brown fox jumps over the lazy dog.", 48.0, 230.0, 42.0, 0xFF111827);
    g_renderer->DrawString(g_image, L"0123456789  !?&$%@#  ABC xyz", 48.0, 290.0, 38.0, 0xFF1F2630);
    DrawGlyphGrid(g_image, *g_renderer, 48, 340);

    const int inspectorLeft = std::max(650, width - 430);
    DrawLargeAInspector(g_image, inspectorLeft, 24, width - 24, height - 24);
    DrawTooltip(g_image);
}

void RenderBlank(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = std::max(1, static_cast<int>(rc.right - rc.left) - kSidebarWidth);
    const int height = std::max(1, static_cast<int>(rc.bottom - rc.top));
    g_image.Resize(width, height);
    g_image.Clear(kBlankBackground);
    SetWindowTextW(hwnd, L"font1");
}

void RenderScene(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = std::max(1, static_cast<int>(rc.right - rc.left) - kSidebarWidth);
    const int height = std::max(1, static_cast<int>(rc.bottom - rc.top));

    if (!g_renderer) {
        RenderBlank(hwnd);
        return;
    }

    RenderContent(width, height);
    std::wstring title = L"font1 - ";
    title += g_status;
    SetWindowTextW(hwnd, title.c_str());
}

std::wstring FileNameFromPath(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring Lowercase(std::wstring value)
{
    for (wchar_t& ch : value) ch = static_cast<wchar_t>(std::towlower(ch));
    return value;
}

bool IsFontFileName(const std::wstring& name)
{
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;

    const std::wstring ext = Lowercase(name.substr(dot));
    return ext == L".ttf" || ext == L".ttc" || ext == L".otf" || ext == L".fon" || ext == L".fnt";
}

bool MatchesFontCategory(const std::wstring& name, FontCategory category)
{
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;

    const std::wstring ext = Lowercase(name.substr(dot));
    switch (category) {
    case FontCategory::Ttf:
        return ext == L".ttf";
    case FontCategory::Ttc:
        return ext == L".ttc";
    case FontCategory::Otf:
        return ext == L".otf";
    case FontCategory::Fon:
        return ext == L".fon";
    case FontCategory::Fnt:
        return ext == L".fnt";
    default:
        return false;
    }
}

std::vector<FontFileItem> EnumerateFontFiles(const std::wstring& directory)
{
    std::vector<FontFileItem> files;
    const std::wstring pattern = directory + L"\\*";

    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return files;

    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const std::wstring name = data.cFileName;
        if (!IsFontFileName(name)) continue;
        if (!MatchesFontCategory(name, g_fontCategory)) continue;
        files.push_back({ name, directory + L"\\" + name });
    } while (FindNextFileW(find, &data));

    FindClose(find);

    std::sort(files.begin(), files.end(), [](const FontFileItem& a, const FontFileItem& b) {
        return Lowercase(a.name) < Lowercase(b.name);
    });
    return files;
}

void LayoutMainWindow(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int height = rc.bottom - rc.top;
    constexpr int pad = 10;
    constexpr int categoryH = 28;
    constexpr int comboDropH = 150;
    constexpr int labelH = 20;
    constexpr int pathH = 24;
    constexpr int buttonH = 28;

    if (g_fontDirectoryLabel) {
        MoveWindow(g_fontDirectoryLabel, pad, pad, kSidebarWidth - pad * 2, labelH, TRUE);
    }
    if (g_fontCategoryCombo) {
        MoveWindow(g_fontCategoryCombo, pad, pad + labelH + 8, kSidebarWidth - pad * 2, comboDropH, TRUE);
    }
    if (g_fontList) {
        const int listTop = pad + labelH + 8 + categoryH + 6;
        const int listH = std::max(40, height - listTop - pad * 3 - pathH - buttonH);
        MoveWindow(g_fontList, pad, listTop, kSidebarWidth - pad * 2, listH, TRUE);
    }
    if (g_fontPathEdit) {
        MoveWindow(g_fontPathEdit, pad, height - pad * 2 - buttonH - pathH, kSidebarWidth - pad * 2, pathH, TRUE);
    }
    if (g_fontOpenButton) {
        MoveWindow(g_fontOpenButton, pad, height - pad - buttonH, kSidebarWidth - pad * 2, buttonH, TRUE);
    }
}

void UpdateSelectedFontPath();

void PopulateFontList()
{
    if (!g_fontList) return;

    SendMessageW(g_fontList, LB_RESETCONTENT, 0, 0);
    if (g_fontPathEdit) SetWindowTextW(g_fontPathEdit, L"");
    if (g_fontDirectoryLabel) SetWindowTextW(g_fontDirectoryLabel, g_fontDirectory.c_str());
    g_fontFiles = EnumerateFontFiles(g_fontDirectory);

    for (const FontFileItem& file : g_fontFiles) {
        SendMessageW(g_fontList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(file.name.c_str()));
    }

    if (g_fontFiles.empty()) {
        SendMessageW(g_fontList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No font files found."));
        EnableWindow(g_fontList, FALSE);
        if (g_fontOpenButton) EnableWindow(g_fontOpenButton, FALSE);
    } else {
        EnableWindow(g_fontList, TRUE);
        if (g_fontOpenButton) EnableWindow(g_fontOpenButton, TRUE);
        SendMessageW(g_fontList, LB_SETCURSEL, 0, 0);
        UpdateSelectedFontPath();
    }
}

void UpdateSelectedFontPath()
{
    if (!g_fontList || !g_fontPathEdit) return;

    const int selected = static_cast<int>(SendMessageW(g_fontList, LB_GETCURSEL, 0, 0));
    if (selected == LB_ERR || selected >= static_cast<int>(g_fontFiles.size())) {
        SetWindowTextW(g_fontPathEdit, L"");
        return;
    }

    SetWindowTextW(g_fontPathEdit, g_fontFiles[static_cast<size_t>(selected)].path.c_str());
}

bool LoadFont(HWND hwnd, const std::wstring& path)
{
    font::TtfFont nextFont;
    std::wstring error;
    if (!nextFont.LoadFromFile(path, &error)) {
        g_status = L"failed to load " + path + L": " + error;
        MessageBoxW(hwnd, g_status.c_str(), L"font1", MB_ICONERROR | MB_OK);
        return false;
    }

    g_font = std::move(nextFont);
    g_fontPath = path;
    g_status = L"loaded " + FileNameFromPath(path);
    g_renderer = std::make_unique<font::FontRenderer>(g_font);
    g_tooltip = TooltipInfo();
    RenderScene(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void OpenSelectedFont(HWND hwnd)
{
    if (!g_fontList) return;

    const int selected = static_cast<int>(SendMessageW(g_fontList, LB_GETCURSEL, 0, 0));
    if (selected == LB_ERR || selected >= static_cast<int>(g_fontFiles.size())) return;

    LoadFont(hwnd, g_fontFiles[static_cast<size_t>(selected)].path);
}

bool SelectFontDirectory(HWND hwnd)
{
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        MessageBoxW(hwnd, L"Could not open the directory selector.", L"font1", MB_ICONERROR | MB_OK);
        return false;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(L"Select font directory");

    IShellItem* initialFolder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(g_fontDirectory.c_str(), nullptr, IID_PPV_ARGS(&initialFolder)))) {
        dialog->SetFolder(initialFolder);
        initialFolder->Release();
    }

    bool selected = false;
    if (SUCCEEDED(dialog->Show(hwnd))) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                g_fontDirectory = path;
                CoTaskMemFree(path);
                PopulateFontList();
                selected = true;
            }
            result->Release();
        }
    }

    dialog->Release();
    return selected;
}

bool SaveBmp(const char* path, const font::Image& image)
{
    const int rowBytes = image.width * 4;
    const uint32_t pixelBytes = static_cast<uint32_t>(rowBytes * image.height);
    const uint32_t fileBytes = 14 + 40 + pixelBytes;

    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    const uint8_t fileHeader[14] = {
        'B', 'M',
        static_cast<uint8_t>(fileBytes), static_cast<uint8_t>(fileBytes >> 8), static_cast<uint8_t>(fileBytes >> 16), static_cast<uint8_t>(fileBytes >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0
    };
    file.write(reinterpret_cast<const char*>(fileHeader), sizeof(fileHeader));

    const int32_t topDownHeight = -image.height;
    const uint8_t infoHeader[40] = {
        40, 0, 0, 0,
        static_cast<uint8_t>(image.width), static_cast<uint8_t>(image.width >> 8), static_cast<uint8_t>(image.width >> 16), static_cast<uint8_t>(image.width >> 24),
        static_cast<uint8_t>(topDownHeight), static_cast<uint8_t>(topDownHeight >> 8), static_cast<uint8_t>(topDownHeight >> 16), static_cast<uint8_t>(topDownHeight >> 24),
        1, 0,
        32, 0,
        0, 0, 0, 0,
        static_cast<uint8_t>(pixelBytes), static_cast<uint8_t>(pixelBytes >> 8), static_cast<uint8_t>(pixelBytes >> 16), static_cast<uint8_t>(pixelBytes >> 24),
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };
    file.write(reinterpret_cast<const char*>(infoHeader), sizeof(infoHeader));
    file.write(reinterpret_cast<const char*>(image.pixels.data()), pixelBytes);
    return true;
}

int RunSmoke()
{
    std::wstring error;
    if (!g_font.LoadFromFile(kFontPath, &error)) return 2;
    g_renderer = std::make_unique<font::FontRenderer>(g_font);
    RenderContent(900, 620);

    size_t ink = 0;
    for (uint32_t pixel : g_image.pixels) {
        if (pixel != 0xFFE7E2D8 && pixel != 0xFFF8F7F2 && pixel != 0xFFF4F1EA) ++ink;
    }

    if (!SaveBmp("smoke.bmp", g_image)) return 3;
    return ink > 1000 ? 0 : 4;
}

void PaintImage(HWND hwnd)
{
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT sidebar{ 0, 0, std::min(kSidebarWidth, static_cast<int>(rc.right - rc.left)), rc.bottom };
    FillRect(hdc, &sidebar, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    RECT separator{ kSidebarWidth - 1, 0, kSidebarWidth, rc.bottom };
    FillRect(hdc, &separator, reinterpret_cast<HBRUSH>(COLOR_3DSHADOW + 1));

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_image.width;
    bmi.bmiHeader.biHeight = -g_image.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(
        hdc,
        kSidebarWidth,
        0,
        g_image.width,
        g_image.height,
        0,
        0,
        g_image.width,
        g_image.height,
        g_image.pixels.data(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_fontCategoryCombo = CreateWindowExW(
            0,
            L"COMBOBOX",
            nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontCategoryId)),
            nullptr,
            nullptr);
        SendMessageW(g_fontCategoryCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TrueType fonts (.ttf)"));
        SendMessageW(g_fontCategoryCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"TrueType collections (.ttc)"));
        SendMessageW(g_fontCategoryCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"OpenType fonts (.otf)"));
        SendMessageW(g_fontCategoryCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"FON fonts (.fon)"));
        SendMessageW(g_fontCategoryCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"FNT fonts (.fnt)"));
        SendMessageW(g_fontCategoryCombo, CB_SETCURSEL, 0, 0);

        g_fontDirectoryLabel = CreateWindowExW(
            0,
            L"STATIC",
            g_fontDirectory.c_str(),
            WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_LEFTNOWORDWRAP,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontDirectoryLabelId)),
            nullptr,
            nullptr);
        g_fontList = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontListId)),
            nullptr,
            nullptr);
        g_fontPathEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            nullptr,
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontPathId)),
            nullptr,
            nullptr);
        g_fontOpenButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Open",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontOpenId)),
            nullptr,
            nullptr);

        PopulateFontList();
        LayoutMainWindow(hwnd);
        RenderBlank(hwnd);
        return 0;
    case WM_SIZE:
        LayoutMainWindow(hwnd);
        if (!g_inSizeMove) {
            SetTimer(hwnd, kResizeTimer, kResizeDebounceMs, nullptr);
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        g_inSizeMove = true;
        KillTimer(hwnd, kResizeTimer);
        return 0;
    case WM_EXITSIZEMOVE:
        g_inSizeMove = false;
        RenderScene(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (g_renderer) {
            if (!g_trackingMouse) {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                g_trackingMouse = true;
            }

            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int mouseX = GET_X_LPARAM(lParam) - kSidebarWidth;
            const int mouseY = GET_Y_LPARAM(lParam);
            const int canvasWidth = std::max(1, static_cast<int>(rc.right - rc.left) - kSidebarWidth);
            TooltipInfo next = mouseX >= 0 ? HitTestInspector(mouseX, mouseY, canvasWidth, static_cast<int>(rc.bottom - rc.top)) : TooltipInfo();
            if (!SameTooltip(g_tooltip, next)) {
                g_tooltip = next;
                RenderScene(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        g_trackingMouse = false;
        if (g_tooltip.visible) {
            g_tooltip = TooltipInfo();
            RenderScene(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (wParam == kResizeTimer) {
            KillTimer(hwnd, kResizeTimer);
            RenderScene(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_COMMAND:
        if (LOWORD(wParam) == kFontDirectoryLabelId && HIWORD(wParam) == STN_CLICKED) {
            SelectFontDirectory(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kFontCategoryId && HIWORD(wParam) == CBN_SELCHANGE) {
            const int selected = static_cast<int>(SendMessageW(g_fontCategoryCombo, CB_GETCURSEL, 0, 0));
            switch (selected) {
            case 1:
                g_fontCategory = FontCategory::Ttc;
                break;
            case 2:
                g_fontCategory = FontCategory::Otf;
                break;
            case 3:
                g_fontCategory = FontCategory::Fon;
                break;
            case 4:
                g_fontCategory = FontCategory::Fnt;
                break;
            default:
                g_fontCategory = FontCategory::Ttf;
                break;
            }
            PopulateFontList();
            return 0;
        }
        if (LOWORD(wParam) == kFontListId && HIWORD(wParam) == LBN_SELCHANGE) {
            UpdateSelectedFontPath();
            return 0;
        }
        if (LOWORD(wParam) == kFontListId && HIWORD(wParam) == LBN_DBLCLK) {
            OpenSelectedFont(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kFontOpenId) {
            OpenSelectedFont(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_PAINT:
        PaintImage(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    if (wcsstr(GetCommandLineW(), L"--smoke") != nullptr) {
        return RunSmoke();
    }

    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitializeCom = SUCCEEDED(comInit);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        if (shouldUninitializeCom) CoUninitialize();
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClass,
        L"font1",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1100,
        760,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        if (shouldUninitializeCom) CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (shouldUninitializeCom) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
