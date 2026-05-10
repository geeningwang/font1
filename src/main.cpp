#define NOMINMAX
#include <windows.h>

#include "font/FontRenderer.h"
#include "font/TtfFont.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>

namespace {

constexpr wchar_t kWindowClass[] = L"Font1WindowClass";
constexpr wchar_t kFontPath[] = L"C:\\Windows\\Fonts\\times.ttf";

font::TtfFont g_font;
std::unique_ptr<font::FontRenderer> g_renderer;
font::Image g_image;
std::wstring g_status = L"Loading...";
bool g_inSizeMove = false;

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
    if (right - left < 160 || bottom - top < 220) return;

    constexpr uint32_t auxRed = 0xFFFF0000;
    constexpr uint32_t ink = 0xFF111827;
    constexpr uint32_t panel = 0xFFFCFBF7;

    DrawRect(image, left, top, right, bottom, panel);

    const uint16_t glyphIndex = g_font.GlyphIndexForCodepoint(L'A');
    font::GlyphOutline outline;
    if (!g_font.LoadGlyph(glyphIndex, outline)) return;

    const int panelWidth = right - left;
    const int panelHeight = bottom - top;
    const double scaleByWidth = (panelWidth - 64.0) / std::max(1.0, static_cast<double>(outline.advanceWidth));
    const double scaleByHeight = (panelHeight - 70.0) /
        std::max(1.0, static_cast<double>(g_font.Ascender() - g_font.Descender()));
    const double scale = std::max(0.02, std::min(scaleByWidth, scaleByHeight));
    const double pixelSize = scale * g_font.UnitsPerEm();

    const double originX = left + 32.0 - outline.leftSideBearing * scale;
    const double baselineY = top + 42.0 + g_font.Ascender() * scale;
    auto sx = [&](double fontX) { return originX + fontX * scale; };
    auto sy = [&](double fontY) { return baselineY - fontY * scale; };

    const double lineLeft = left + 12.0;
    const double lineRight = right - 12.0;
    const double ascenderY = sy(g_font.Ascender());
    const double descenderY = sy(g_font.Descender());
    const double baseline = sy(0.0);
    const double emTopY = sy(g_font.UnitsPerEm());
    const double lineGapTopY = sy(g_font.Ascender() + g_font.LineGap());
    const double bboxTop = sy(outline.yMax);
    const double bboxBottom = sy(outline.yMin);

    // Horizontal metrics and guide lines.
    DrawLine(image, lineLeft, baseline, lineRight, baseline, auxRed);
    DrawLine(image, lineLeft, ascenderY, lineRight, ascenderY, auxRed);
    DrawLine(image, lineLeft, descenderY, lineRight, descenderY, auxRed);
    DrawLine(image, lineLeft, emTopY, lineRight, emTopY, auxRed);
    DrawLine(image, lineLeft, lineGapTopY, lineRight, lineGapTopY, auxRed);
    DrawLine(image, lineLeft, bboxTop, lineRight, bboxTop, auxRed);
    DrawLine(image, lineLeft, bboxBottom, lineRight, bboxBottom, auxRed);

    // Vertical metrics: origin, left side bearing, black box, and advance.
    DrawLine(image, sx(0.0), top + 12.0, sx(0.0), bottom - 12.0, auxRed);
    DrawLine(image, sx(outline.leftSideBearing), top + 12.0, sx(outline.leftSideBearing), bottom - 12.0, auxRed);
    DrawLine(image, sx(outline.xMin), top + 12.0, sx(outline.xMin), bottom - 12.0, auxRed);
    DrawLine(image, sx(outline.xMax), top + 12.0, sx(outline.xMax), bottom - 12.0, auxRed);
    DrawLine(image, sx(outline.advanceWidth), top + 12.0, sx(outline.advanceWidth), bottom - 12.0, auxRed);

    DrawRectOutline(image, sx(outline.xMin), sy(outline.yMax), sx(outline.xMax), sy(outline.yMin), auxRed);
    DrawRectOutline(image, sx(0.0), sy(g_font.UnitsPerEm()), sx(g_font.UnitsPerEm()), sy(0.0), auxRed);

    g_renderer->DrawGlyph(image, glyphIndex, originX, baselineY, pixelSize, ink);

    // Draw the flattened outline geometry over the filled glyph.
    for (const font::Contour& contour : outline.contours) {
        if (contour.points.size() < 2) continue;
        for (size_t i = 0, j = contour.points.size() - 1; i < contour.points.size(); j = i++) {
            DrawLine(image, sx(contour.points[j].x), sy(contour.points[j].y), sx(contour.points[i].x), sy(contour.points[i].y), auxRed);
        }
    }
}

void RenderContent(int width, int height)
{
    g_image.Resize(width, height);
    g_image.Clear(0xFFE7E2D8);

    DrawRect(g_image, 24, 24, width - 24, 158, 0xFFF8F7F2);
    g_renderer->DrawString(g_image, L"Times New Roman", 48.0, 91.0, 54.0, 0xFF111827);
    g_renderer->DrawString(g_image, L"Custom TrueType parser + software rasterizer", 50.0, 132.0, 20.0, 0xFF46515F);

    g_renderer->DrawString(g_image, L"The quick brown fox jumps over the lazy dog.", 48.0, 230.0, 42.0, 0xFF111827);
    g_renderer->DrawString(g_image, L"0123456789  !?&$%@#  ABC xyz", 48.0, 290.0, 38.0, 0xFF1F2630);
    DrawGlyphGrid(g_image, *g_renderer, 48, 340);

    const int inspectorLeft = std::max(650, width - 430);
    DrawLargeAInspector(g_image, inspectorLeft, 24, width - 24, height - 24);
}

void RenderScene(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = std::max(1, static_cast<int>(rc.right - rc.left));
    const int height = std::max(1, static_cast<int>(rc.bottom - rc.top));
    RenderContent(width, height);
    std::wstring title = L"font1 - ";
    title += g_status;
    SetWindowTextW(hwnd, title.c_str());
}

bool LoadFont(HWND hwnd)
{
    std::wstring error;
    if (!g_font.LoadFromFile(kFontPath, &error)) {
        g_status = L"failed to load C:\\Windows\\Fonts\\times.ttf: " + error;
        MessageBoxW(hwnd, g_status.c_str(), L"font1", MB_ICONERROR | MB_OK);
        return false;
    }

    g_status = L"loaded C:\\Windows\\Fonts\\times.ttf";
    g_renderer = std::make_unique<font::FontRenderer>(g_font);
    return true;
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

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_image.width;
    bmi.bmiHeader.biHeight = -g_image.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(
        hdc,
        0,
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
        if (LoadFont(hwnd)) {
            RenderScene(hwnd);
        }
        return 0;
    case WM_SIZE:
        if (g_renderer && !g_inSizeMove) {
            SetTimer(hwnd, kResizeTimer, kResizeDebounceMs, nullptr);
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        g_inSizeMove = true;
        KillTimer(hwnd, kResizeTimer);
        return 0;
    case WM_EXITSIZEMOVE:
        g_inSizeMove = false;
        if (g_renderer) {
            RenderScene(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (wParam == kResizeTimer) {
            KillTimer(hwnd, kResizeTimer);
            if (g_renderer) {
                RenderScene(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
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

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) return 1;

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

    if (!hwnd) return 1;

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
