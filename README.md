# font1

A TrueType font viewer and renderer for Windows with a custom font parser and software rasterizer.

## Overview

font1 is a Windows desktop application that allows you to browse, view, and inspect TrueType font files. It includes a complete TrueType font parser written from scratch and a software rasterizer that renders glyphs without using system font APIs.

## Features

- **Font Browser**: Browse font files from any directory on your system
- **Font Format Support**: TrueType (.ttf), TrueType Collections (.ttc), OpenType (.otf), FON (.fon), and FNT (.fnt) formats
- **Live Preview**: View rendered text samples with the selected font
- **Glyph Grid**: Display a grid of commonly used characters
- **Font Inspector**: Detailed visualization of glyph metrics including:
  - Baseline, ascender, descender, and line gap
  - Em square and glyph bounding boxes
  - Left side bearing and advance width
  - Interactive tooltips for all metric lines
- **Custom Implementation**: No dependency on system font rendering APIs
- **Software Rasterizer**: Pure software implementation of TrueType outline rendering

## Building

This project uses Visual Studio on Windows:

1. Open `src/font1.sln` in Visual Studio
2. Build the solution (Debug or Release configuration)
3. Run the executable

## Usage

Launch the application to see the font browser interface. By default, it opens the Windows Fonts directory (`C:\Windows\Fonts`).

- **Browse Fonts**: Select a font category from the dropdown menu and choose a font from the list
- **Open Font**: Double-click a font or click the "Open" button to view it
- **Change Directory**: Click on the directory path label to browse to a different folder
- **Inspect Metrics**: Hover over the metric lines in the inspector panel to see detailed explanations

## Project Structure

```
src/
├── main.cpp              # Windows GUI application and rendering logic
├── font/
│   ├── TtfFont.h         # TrueType font parser interface
│   ├── TtfFont.cpp       # TrueType font parser implementation
│   ├── FontRenderer.h    # Software rasterizer interface
│   └── FontRenderer.cpp  # Software rasterizer implementation
```

## Technical Details

### TrueType Parser

The `TtfFont` class implements a complete TrueType font parser that reads and interprets font tables including:
- `cmap` - Character to glyph mapping
- `glyf` - Glyph outlines (simple and composite glyphs)
- `head` - Font header
- `hhea` - Horizontal header
- `hmtx` - Horizontal metrics
- `loca` - Glyph locations
- `maxp` - Maximum profile
- `name` - Font naming table
- `kern` - Kerning pairs
- `GSUB` - Glyph substitution (ligatures)
- `GPOS` - Glyph positioning (pair adjustments)

### Software Rasterizer

The `FontRenderer` class provides software rasterization of TrueType outlines:
- Converts quadratic Bézier curves to line segments
- Renders glyphs using alpha blending
- Implements glyph caching for performance
- Supports text shaping with kerning and ligatures

### GUI Features

- Sidebar with font file browser
- Main canvas for rendering text samples and previews
- Inspector panel showing detailed glyph metrics for the letter "A"
- Interactive tooltips explaining font metrics
- Loading indicators during font parsing
- Responsive layout that adapts to window resizing

## Testing

The application includes a smoke test mode:

```bash
font1.exe --smoke
```

This renders a test scene and saves it as `smoke.bmp`.

## License

This project does not currently have a specified license.
