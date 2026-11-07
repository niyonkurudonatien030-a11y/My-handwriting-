// Quick self-test of core.h  (builds on any system):  g++ -std=c++17 tests/test_core.cpp -Isrc -o test_core && ./test_core
#include "core.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{ if(!(x)){ std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1);} }while(0)
int main() {
    using namespace core;
    Document d;
    d.insertText(L"hello"); CHECK(d.lines[0] == L"hello" && d.caret.c == 5);
    d.insertText(L"\nworld"); CHECK(d.lines.size() == 2 && d.lines[1] == L"world");
    d.backspace(); CHECK(d.lines[1] == L"worl");
    d.vertical(-1, false); CHECK(d.caret.r == 0 && d.caret.c == 4);
    d.end(false); d.right(false); CHECK(d.caret.r == 1 && d.caret.c == 0);
    d.backspace(); CHECK(d.lines.size() == 1 && d.lines[0] == L"helloworl" && d.caret.c == 5);
    d.selectAll(); CHECK(d.selectedText() == L"helloworl");
    d.insertText(L"x\ny"); CHECK(d.lines.size() == 2 && d.lines[0] == L"x" && d.lines[1] == L"y");
    d.docStart(false); d.right(true); d.right(true); CHECK(d.selectedText() == L"x\n");
    d.del(); CHECK(d.lines.size() == 1 && d.lines[0] == L"y");
    // UTF-8 round trip incl. non-ASCII
    std::string u = "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80\nline2\r\nline3";
    d.loadFromUtf8(u); CHECK(d.lines.size() == 3 && d.lines[2] == L"line3");
    Document e; e.loadFromUtf8(d.toUtf8()); CHECK(e.lines == d.lines);
    // glyphs.txt parsing
    GlyphMap g;
    parseGlyphMap("@cell_width=50 # hi\n# c\na = g01.png\nU+0042=b.png  # note\n7=seven.png\nbad line\n@case_insensitive=0\n", g);
    CHECK(g.s.cellW == 50 && !g.s.caseInsensitive);
    CHECK(g.files.size() == 3 && g.files[L'a'] == L"g01.png" && g.files[L'B'] == L"b.png" && g.files[L'7'] == L"seven.png");
    CHECK(g.warnings.size() == 1);
    // the shipped demo file
    FILE* f = std::fopen("glyphs/glyphs.txt", "rb");
    if (f) { std::string t; char buf[4096]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) t.append(buf, n); std::fclose(f);
        GlyphMap m; parseGlyphMap(t, m); CHECK(m.files.size() == 18 && m.warnings.empty()); std::printf("demo glyphs.txt: %zu mappings OK\n", m.files.size()); }
    std::printf("ALL TESTS PASSED\n");
}
