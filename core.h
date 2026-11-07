#pragma once
// =====================================================================
//  core.h  -  the editor's "brain".  Plain C++17, NO Windows code here,
//  so it can be read, tested and reused on any system (even Android NDK).
//    * Document   : the text, the cursor, the selection, typing/deleting
//    * GlyphMap   : parsing of glyphs.txt  (key -> picture file + settings)
//    * UTF-8 helpers : reading and saving text files
//  IMPORTANT IDEA: the Document always stores the REAL characters you typed
//  (a, b, 7 ...).  Pictures are only used when DRAWING.  So saved files are
//  ordinary text, and you can always switch the pictures off (key F2).
// =====================================================================
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace core {

// ---------- UTF-8 <-> wide string ------------------------------------
inline std::wstring utf8ToW(const std::string& s) {
    std::wstring o; size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i]; uint32_t cp; size_t len;
        if (c < 0x80)              { cp = c;        len = 1; }
        else if ((c >> 5) == 6)    { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 14)   { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 30)   { cp = c & 0x07; len = 4; }
        else                       { cp = 0xFFFD;   len = 1; }
        if (len > 1) {
            if (i + len > n) { cp = 0xFFFD; len = n - i; }
            else for (size_t k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        }
        i += len;
        if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
            cp -= 0x10000;
            o += (wchar_t)(0xD800 + (cp >> 10));
            o += (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else o += (wchar_t)cp;
    }
    return o;
}

inline std::string wToUtf8(const std::wstring& w) {
    std::string o;
    for (size_t i = 0; i < w.size(); i++) {
        uint32_t cp = (uint32_t)w[i];
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size()) {
            uint32_t lo = (uint32_t)w[i + 1];
            if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i++; }
        }
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    return o;
}

// ---------- glyphs.txt ------------------------------------------------
struct Settings {
    int cellW = 44, cellH = 64, glyphPad = 6, fontPx = 34;
    bool caseInsensitive = true;
    std::wstring fontName = L"Consolas";
};

struct GlyphMap {
    Settings s;
    std::map<wchar_t, std::wstring> files;   // typed character -> picture file name
    std::vector<std::string> warnings;       // problems found while reading
};

inline std::string trim(const std::string& t) {
    size_t a = 0, b = t.size();
    while (a < b && (t[a] == ' ' || t[a] == '\t' || t[a] == '\r' || t[a] == '\n')) a++;
    while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t' || t[b - 1] == '\r' || t[b - 1] == '\n')) b--;
    return t.substr(a, b - a);
}

inline std::string stripComment(const std::string& v) {
    size_t p = v.find(" #");
    size_t q = v.find("\t#");
    size_t cut = std::min(p, q);
    return cut == std::string::npos ? v : v.substr(0, cut);
}

inline void parseGlyphMap(const std::string& text, GlyphMap& out) {
    size_t pos = 0; int lineNo = 0;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) pos = 3;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        lineNo++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '@') {                                    // setting
            size_t eq = line.find('=');
            if (eq == std::string::npos) { out.warnings.push_back("line " + std::to_string(lineNo) + ": setting needs '='"); continue; }
            std::string k = trim(line.substr(1, eq - 1));
            std::string v = trim(stripComment(line.substr(eq + 1)));
            try {
                if (k == "cell_width") out.s.cellW = std::max(8, std::stoi(v));
                else if (k == "cell_height") out.s.cellH = std::max(8, std::stoi(v));
                else if (k == "glyph_padding") out.s.glyphPad = std::max(0, std::stoi(v));
                else if (k == "font_px") out.s.fontPx = std::max(6, std::stoi(v));
                else if (k == "case_insensitive") out.s.caseInsensitive = (std::stoi(v) != 0);
                else if (k == "font") out.s.fontName = utf8ToW(v);
                else out.warnings.push_back("line " + std::to_string(lineNo) + ": unknown setting @" + k);
            } catch (...) { out.warnings.push_back("line " + std::to_string(lineNo) + ": bad number for @" + k); }
            continue;
        }

        size_t eq = line.find('=');                              // mapping  key = file
        if (eq == std::string::npos || eq == 0) { out.warnings.push_back("line " + std::to_string(lineNo) + ": expected  key = file.png"); continue; }
        std::string left = trim(line.substr(0, eq));
        std::string right = trim(stripComment(line.substr(eq + 1)));
        wchar_t key = 0;
        if (left.size() >= 3 && (left[0] == 'U' || left[0] == 'u') && left[1] == '+') {
            try { key = (wchar_t)std::stoul(left.substr(2), nullptr, 16); } catch (...) { key = 0; }
        } else {
            std::wstring w = utf8ToW(left);
            if (w.size() == 1) key = w[0];
        }
        if (key == 0 || right.empty()) { out.warnings.push_back("line " + std::to_string(lineNo) + ": could not read the key or file name"); continue; }
        out.files[key] = utf8ToW(right);
    }
}

// ---------- the document ----------------------------------------------
struct Pos { int r = 0, c = 0; };
inline bool operator==(Pos a, Pos b) { return a.r == b.r && a.c == b.c; }
inline bool operator<(Pos a, Pos b) { return a.r < b.r || (a.r == b.r && a.c < b.c); }

class Document {
public:
    std::vector<std::wstring> lines;
    Pos caret, anchor;          // selection = text between anchor and caret
    int wantCol = 0;            // remembered column for Up/Down
    bool dirty = false;         // unsaved changes?

    Document() { lines.push_back(L""); }

    void clear() { lines.assign(1, L""); caret = anchor = Pos(); wantCol = 0; dirty = false; }
    int lineLen(int r) const { return (int)lines[r].size(); }
    bool hasSel() const { return !(caret == anchor); }
    void selOrdered(Pos& a, Pos& b) const { if (anchor < caret) { a = anchor; b = caret; } else { a = caret; b = anchor; } }

    std::wstring selectedText() const {
        if (!hasSel()) return L"";
        Pos a, b; selOrdered(a, b);
        if (a.r == b.r) return lines[a.r].substr(a.c, b.c - a.c);
        std::wstring s = lines[a.r].substr(a.c);
        for (int r = a.r + 1; r < b.r; r++) { s += L'\n'; s += lines[r]; }
        s += L'\n'; s += lines[b.r].substr(0, b.c);
        return s;
    }

    bool deleteSelection() {
        if (!hasSel()) return false;
        Pos a, b; selOrdered(a, b);
        if (a.r == b.r) lines[a.r].erase(a.c, b.c - a.c);
        else {
            lines[a.r] = lines[a.r].substr(0, a.c) + lines[b.r].substr(b.c);
            lines.erase(lines.begin() + a.r + 1, lines.begin() + b.r + 1);
        }
        caret = anchor = a; wantCol = a.c; dirty = true;
        return true;
    }

    // Type / paste text (handles new lines, replaces any selection).
    void insertText(const std::wstring& t) {
        deleteSelection();
        for (wchar_t ch : t) {
            if (ch == L'\r') continue;
            if (ch == L'\n') {
                std::wstring rest = lines[caret.r].substr(caret.c);
                lines[caret.r].erase(caret.c);
                lines.insert(lines.begin() + caret.r + 1, rest);
                caret.r++; caret.c = 0;
            } else { lines[caret.r].insert(caret.c, 1, ch); caret.c++; }
        }
        anchor = caret; wantCol = caret.c; dirty = true;
    }

    void backspace() {
        if (deleteSelection()) return;
        if (caret.c > 0) { lines[caret.r].erase(caret.c - 1, 1); caret.c--; }
        else if (caret.r > 0) {
            int pc = lineLen(caret.r - 1);
            lines[caret.r - 1] += lines[caret.r];
            lines.erase(lines.begin() + caret.r);
            caret.r--; caret.c = pc;
        } else return;
        anchor = caret; wantCol = caret.c; dirty = true;
    }

    void del() {
        if (deleteSelection()) return;
        if (caret.c < lineLen(caret.r)) lines[caret.r].erase(caret.c, 1);
        else if (caret.r + 1 < (int)lines.size()) {
            lines[caret.r] += lines[caret.r + 1];
            lines.erase(lines.begin() + caret.r + 1);
        } else return;
        anchor = caret; wantCol = caret.c; dirty = true;
    }

    // ----- moving the cursor (extend = Shift held -> grow the selection) -----
    void setCaret(Pos p, bool extend, bool keepWant = false) {
        caret = p; if (!extend) anchor = p;
        if (!keepWant) wantCol = p.c;
    }
    void left(bool ext) {
        if (!ext && hasSel()) { Pos a, b; selOrdered(a, b); setCaret(a, false); return; }
        Pos p = caret;
        if (p.c > 0) p.c--; else if (p.r > 0) { p.r--; p.c = lineLen(p.r); }
        setCaret(p, ext);
    }
    void right(bool ext) {
        if (!ext && hasSel()) { Pos a, b; selOrdered(a, b); setCaret(b, false); return; }
        Pos p = caret;
        if (p.c < lineLen(p.r)) p.c++; else if (p.r + 1 < (int)lines.size()) { p.r++; p.c = 0; }
        setCaret(p, ext);
    }
    void vertical(int delta, bool ext) {
        Pos p = caret; int nr = std::max(0, std::min((int)lines.size() - 1, p.r + delta));
        if (nr == p.r) { p.c = delta < 0 ? 0 : lineLen(p.r); setCaret(p, ext); return; }
        p.r = nr; p.c = std::min(wantCol, lineLen(nr));
        setCaret(p, ext, true);
    }
    void home(bool ext) { Pos p = caret; p.c = 0; setCaret(p, ext); }
    void end(bool ext) { Pos p = caret; p.c = lineLen(p.r); setCaret(p, ext); }
    void docStart(bool ext) { setCaret(Pos(), ext); }
    void docEnd(bool ext) { Pos p; p.r = (int)lines.size() - 1; p.c = lineLen(p.r); setCaret(p, ext); }
    void selectAll() { anchor = Pos(); Pos p; p.r = (int)lines.size() - 1; p.c = lineLen(p.r); caret = p; wantCol = p.c; }

    // ----- files -----
    void loadFromUtf8(const std::string& bytes) {
        size_t st = 0;
        if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) st = 3;
        std::wstring w = utf8ToW(bytes.substr(st));
        lines.clear(); std::wstring cur;
        for (wchar_t ch : w) {
            if (ch == L'\n') { lines.push_back(cur); cur.clear(); }
            else if (ch != L'\r') cur += ch;
        }
        lines.push_back(cur);
        caret = anchor = Pos(); wantCol = 0; dirty = false;
    }
    std::string toUtf8() const {          // saved with Windows line endings
        std::wstring w;
        for (size_t i = 0; i < lines.size(); i++) { w += lines[i]; if (i + 1 < lines.size()) w += L"\r\n"; }
        return wToUtf8(w);
    }
};

}  // namespace core
