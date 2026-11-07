#pragma once
// =====================================================================
//  config.h  -  the few places YOU are expected to edit
// =====================================================================

// >>> 1. PUT THE FOLDER THAT HOLDS YOUR GLYPH PNGs + glyphs.txt HERE <<<
//     Examples:   L"C:\\Users\\Donatien\\Documents\\MyGlyphs"
//                 L"D:\\glyphs"
//     Remember: inside a C++ string every backslash is written twice  \\
//     Leave it as L"" to use the SAME folder as the program (GlyphEditor.exe).
#define GLYPH_FOLDER L""

// >>> 2. OPTIONAL: a text file to open automatically when the editor starts <<<
//     Example:   L"C:\\Users\\Donatien\\Documents\\notes.txt"      (L"" = start empty)
#define DEFAULT_TEXT_FILE L""

// The name of the mapping file inside the glyph folder.
#define GLYPH_MAP_NAME L"glyphs.txt"

// Spaces inserted when you press the Tab key.
#define TAB_WIDTH 4

// Window title and start size.
#define APP_TITLE L"Glyph Editor"
#define START_WIDTH  1000
#define START_HEIGHT 700

// Colours (R, G, B).
#define COLOR_BACKGROUND 255, 255, 255
#define COLOR_TEXT         0,   0,   0   // for characters that have no picture
#define COLOR_SELECTION  170, 205, 255
#define COLOR_CARET       30,  30, 200
