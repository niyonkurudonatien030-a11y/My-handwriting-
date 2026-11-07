// =====================================================================
//  main_win.cpp  -  Glyph Editor for Windows (Win32 + GDI+, no extra libraries)
//  You type a normal key; the editor draws YOUR picture for it.
//  The document itself always keeps the real letters (see core.h).
// =====================================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <algorithm>
using std::min;           // gdiplus.h needs these two names in the global scope
using std::max;
#include <windows.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "core.h"
#include "config.h"
#ifdef EMBED_GLYPHS
#include "embedded_glyphs.h"      // produced by:  python tools/glyph_tool.py pack ...
#endif

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#endif

using namespace Gdiplus;

enum { ID_NEW = 1, ID_OPEN, ID_SAVE, ID_SAVEAS, ID_EXIT, ID_TOGGLE, ID_ABOUT };

struct App {
    HWND hwnd = nullptr;
    core::Document doc;
    core::GlyphMap gmap;
    std::map<wchar_t, Bitmap*> bitmaps;        // typed character -> loaded picture
    std::wstring folder;                       // where glyphs.txt + pictures live
    std::wstring path;                         // current text file ("" = untitled)
    Font* font = nullptr;                      // for characters without a picture
    int scrollX = 0, scrollY = 0;              // in pixels
    bool caretOn = true, dragging = false, showGlyphs = true;
} A;

// ------------------------------------------------------------ files ----
static bool readAll(const std::wstring& p, std::vector<unsigned char>& out) {
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return false;
    unsigned char buf[8192]; size_t n; out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}
static bool writeAll(const std::wstring& p, const std::string& data) {
    FILE* f = _wfopen(p.c_str(), L"wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}
static std::wstring exeFolder() {
    wchar_t buf[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    PathRemoveFileSpecW(buf);
    return buf;
}

// Get a glyph-folder file: from inside the program (portable build) or from disk.
static bool getBytes(const std::wstring& name, std::vector<unsigned char>& out) {
#ifdef EMBED_GLYPHS
    std::string n = core::wToUtf8(name);
    for (unsigned i = 0; i < kEmbeddedFileCount; i++)
        if (n == kEmbeddedFiles[i].name) {
            out.assign(kEmbeddedFiles[i].data, kEmbeddedFiles[i].data + kEmbeddedFiles[i].size);
            return true;
        }
    return false;
#else
    return readAll(A.folder + L"\\" + name, out);
#endif
}

// Decode a PNG from memory into a standalone bitmap we own.
static Bitmap* decodeImage(const std::vector<unsigned char>& bytes) {
    if (bytes.empty()) return nullptr;
    IStream* st = SHCreateMemStream(bytes.data(), (UINT)bytes.size());
    if (!st) return nullptr;
    Bitmap* out = nullptr;
    {
        Bitmap src(st);
        if (src.GetLastStatus() == Ok && src.GetWidth() > 0 && src.GetHeight() > 0) {
            UINT w = src.GetWidth(), h = src.GetHeight();
            out = new Bitmap(w, h, PixelFormat32bppARGB);
            Graphics g(out);
            g.Clear(Color(0, 0, 0, 0));
            g.DrawImage(&src, Rect(0, 0, (INT)w, (INT)h), 0, 0, (INT)w, (INT)h, UnitPixel);
        }
    }
    st->Release();
    return out;
}

static void loadGlyphs() {
#ifdef EMBED_GLYPHS
    A.folder = L"(built into the program)";
#else
    if (GLYPH_FOLDER[0] != 0) A.folder = GLYPH_FOLDER;               // your own folder from config.h
    else {                                                           // otherwise: next to the program
        A.folder = exeFolder();
        std::wstring sub = A.folder + L"\\glyphs";                  // ...inside a "glyphs" subfolder if it exists
        if (PathFileExistsW((sub + L"\\" + GLYPH_MAP_NAME).c_str())) A.folder = sub;
    }
#endif
    std::vector<unsigned char> mapBytes;
    std::wstring problems;
    if (!getBytes(GLYPH_MAP_NAME, mapBytes))
        problems += L"Could not find " GLYPH_MAP_NAME L" in:\n" + A.folder + L"\n\n";
    else
        core::parseGlyphMap(std::string(mapBytes.begin(), mapBytes.end()), A.gmap);

    int missing = 0; std::wstring missingList;
    for (auto& kv : A.gmap.files) {
        std::vector<unsigned char> b;
        Bitmap* bmp = getBytes(kv.second, b) ? decodeImage(b) : nullptr;
        if (bmp) A.bitmaps[kv.first] = bmp;
        else { if (missing++ < 8) missingList += L"  " + kv.second + L"\n"; }
    }
    if (missing) problems += L"These picture files could not be loaded:\n" + missingList + L"\n";
    for (size_t i = 0; i < A.gmap.warnings.size() && i < 5; i++)
        problems += L"glyphs.txt: " + core::utf8ToW(A.gmap.warnings[i]) + L"\n";
    if (!problems.empty()) MessageBoxW(nullptr, problems.c_str(), APP_TITLE, MB_OK | MB_ICONWARNING);

    A.font = new Font(A.gmap.s.fontName.c_str(), (REAL)A.gmap.s.fontPx, FontStyleRegular, UnitPixel);
}

static Bitmap* findBitmap(wchar_t c) {
    auto it = A.bitmaps.find(c);
    if (it != A.bitmaps.end()) return it->second;
    if (A.gmap.s.caseInsensitive) {                          // A <-> a share a picture
        wchar_t o = 0;
        if (c >= L'A' && c <= L'Z') o = c - L'A' + L'a';
        else if (c >= L'a' && c <= L'z') o = c - L'a' + L'A';
        if (o) { it = A.bitmaps.find(o); if (it != A.bitmaps.end()) return it->second; }
    }
    return nullptr;
}

// ------------------------------------------------------------ view ----
static int CW() { return A.gmap.s.cellW; }
static int CH() { return A.gmap.s.cellH; }

static void clientSize(int& w, int& h) { RECT rc = {}; if (A.hwnd) GetClientRect(A.hwnd, &rc); w = rc.right; h = rc.bottom; }

static void updateTitle() {
    std::wstring name = A.path.empty() ? L"Untitled" : A.path.substr(A.path.find_last_of(L"\\/") + 1);
    std::wstring t = (A.doc.dirty ? L"* " : L"") + name + L" - " + APP_TITLE;
    if (!A.showGlyphs) t += L"  [plain letters]";
    SetWindowTextW(A.hwnd, t.c_str());
}

static void clampScroll() {
    int w, h; clientSize(w, h);
    int maxCols = 1;
    for (auto& l : A.doc.lines) maxCols = max(maxCols, (int)l.size());
    int contentW = (maxCols + 2) * CW(), contentH = ((int)A.doc.lines.size() + 1) * CH();
    A.scrollX = max(0, min(A.scrollX, max(0, contentW - w)));
    A.scrollY = max(0, min(A.scrollY, max(0, contentH - h)));
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    si.nMin = 0; si.nMax = contentH; si.nPage = h; si.nPos = A.scrollY;
    SetScrollInfo(A.hwnd, SB_VERT, &si, TRUE);
    si.nMax = contentW; si.nPage = w; si.nPos = A.scrollX;
    SetScrollInfo(A.hwnd, SB_HORZ, &si, TRUE);
}

static void ensureCaretVisible() {
    int w, h; clientSize(w, h);
    int x = A.doc.caret.c * CW(), y = A.doc.caret.r * CH();
    if (x < A.scrollX) A.scrollX = x;
    if (x + CW() * 2 > A.scrollX + w) A.scrollX = x + CW() * 2 - w;
    if (y < A.scrollY) A.scrollY = y;
    if (y + CH() > A.scrollY + h) A.scrollY = y + CH() - h;
    clampScroll();
}

static void refresh(bool follow = true) {
    A.caretOn = true;
    if (follow) ensureCaretVisible(); else clampScroll();
    updateTitle();
    InvalidateRect(A.hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------- drawing ----
static void drawChar(Graphics& g, wchar_t c, int x, int y, StringFormat& fmt, SolidBrush& ink) {
    if (c == L' ') return;
    if (A.showGlyphs) {
        if (Bitmap* b = findBitmap(c)) {
            int pad = A.gmap.s.glyphPad;
            double bw = b->GetWidth(), bh = b->GetHeight();
            double sc = min((CW() - 2.0 * pad) / bw, (CH() - 2.0 * pad) / bh);   // keep proportions
            int dw = (int)(bw * sc), dh = (int)(bh * sc);
            g.DrawImage(b, Rect(x + (CW() - dw) / 2, y + (CH() - dh) / 2, dw, dh));
            return;
        }
    }
    wchar_t s[2] = { c, 0 };
    g.DrawString(s, 1, A.font, RectF((REAL)x, (REAL)y, (REAL)CW(), (REAL)CH()), &fmt, &ink);
}

static void paint(HDC hdc) {
    int W, H; clientSize(W, H);
    if (W <= 0 || H <= 0) return;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ old = SelectObject(mem, bm);
    {
        Graphics g(mem);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.Clear(Color(255, COLOR_BACKGROUND));
        StringFormat fmt(StringFormatFlagsNoWrap);
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        SolidBrush ink(Color(255, COLOR_TEXT)), selBrush(Color(255, COLOR_SELECTION)), caretBrush(Color(255, COLOR_CARET));

        core::Pos a, b; A.doc.selOrdered(a, b);
        int rows = (int)A.doc.lines.size();
        int r0 = A.scrollY / CH(), r1 = min(rows - 1, (A.scrollY + H) / CH());
        for (int r = r0; r <= r1; r++) {
            int y = r * CH() - A.scrollY;
            const std::wstring& line = A.doc.lines[r];
            if (A.doc.hasSel() && r >= a.r && r <= b.r) {            // selection highlight
                int c0 = (r == a.r) ? a.c : 0;
                int c1 = (r == b.r) ? b.c : (int)line.size() + 1;
                g.FillRectangle(&selBrush, c0 * CW() - A.scrollX, y, (c1 - c0) * CW(), CH());
            }
            int cStart = max(0, A.scrollX / CW()), cEnd = min((int)line.size() - 1, (A.scrollX + W) / CW());
            for (int c = cStart; c <= cEnd; c++) drawChar(g, line[c], c * CW() - A.scrollX, y, fmt, ink);
        }
        if (A.caretOn)
            g.FillRectangle(&caretBrush, A.doc.caret.c * CW() - A.scrollX, A.doc.caret.r * CH() - A.scrollY, 2, CH());
    }
    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bm); DeleteDC(mem);
}

// ------------------------------------------------------- clipboard ----
static void copySelection() {
    std::wstring t = A.doc.selectedText();
    if (t.empty()) return;
    std::wstring w;
    for (wchar_t ch : t) { if (ch == L'\n') w += L'\r'; w += ch; }
    if (!OpenClipboard(A.hwnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}
static void pasteClipboard() {
    if (!OpenClipboard(A.hwnd)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) { A.doc.insertText(p); GlobalUnlock(h); }
    }
    CloseClipboard();
}

// ------------------------------------------------------ open / save ----
static bool confirmDiscard() {
    if (!A.doc.dirty) return true;
    int r = MessageBoxW(A.hwnd, L"Save your changes first?", APP_TITLE, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) { SendMessageW(A.hwnd, WM_COMMAND, ID_SAVE, 0); return !A.doc.dirty; }
    return true;
}
static bool pickFile(bool save, std::wstring& outPath) {
    wchar_t buf[MAX_PATH * 2] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = A.hwnd;
    ofn.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = buf; ofn.nMaxFile = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return false;
    outPath = buf;
    return true;
}
static void openPath(const std::wstring& p) {
    std::vector<unsigned char> b;
    if (!readAll(p, b)) { MessageBoxW(A.hwnd, L"Could not open that file.", APP_TITLE, MB_OK | MB_ICONERROR); return; }
    A.doc.loadFromUtf8(std::string(b.begin(), b.end()));
    A.path = p; A.scrollX = A.scrollY = 0;
    refresh();
}
static void saveTo(const std::wstring& p) {
    if (!writeAll(p, A.doc.toUtf8())) { MessageBoxW(A.hwnd, L"Could not save the file.", APP_TITLE, MB_OK | MB_ICONERROR); return; }
    A.path = p; A.doc.dirty = false; updateTitle();
}
static void doSave(bool asNew) {
    std::wstring p = A.path;
    if (asNew || p.empty()) { if (!pickFile(true, p)) return; }
    saveTo(p);
}

// ------------------------------------------------------------ input ----
static core::Pos posFromPoint(int x, int y) {
    core::Pos p;
    p.r = max(0, min((int)A.doc.lines.size() - 1, (y + A.scrollY) / CH()));
    p.c = max(0, min(A.doc.lineLen(p.r), (x + A.scrollX + CW() / 2) / CW()));
    return p;
}

static void onKeyDown(WPARAM vk) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    int w, h; clientSize(w, h); int page = max(1, h / CH() - 1);
    if (ctrl) {
        switch (vk) {
        case 'A': A.doc.selectAll(); break;
        case 'C': copySelection(); return;
        case 'X': copySelection(); A.doc.deleteSelection(); break;
        case 'V': pasteClipboard(); break;
        case 'S': doSave(shift); return;
        case 'O': SendMessageW(A.hwnd, WM_COMMAND, ID_OPEN, 0); return;
        case 'N': SendMessageW(A.hwnd, WM_COMMAND, ID_NEW, 0); return;
        case VK_HOME: A.doc.docStart(shift); break;
        case VK_END: A.doc.docEnd(shift); break;
        default: return;
        }
        refresh(); return;
    }
    switch (vk) {
    case VK_LEFT: A.doc.left(shift); break;
    case VK_RIGHT: A.doc.right(shift); break;
    case VK_UP: A.doc.vertical(-1, shift); break;
    case VK_DOWN: A.doc.vertical(1, shift); break;
    case VK_PRIOR: A.doc.vertical(-page, shift); break;
    case VK_NEXT: A.doc.vertical(page, shift); break;
    case VK_HOME: A.doc.home(shift); break;
    case VK_END: A.doc.end(shift); break;
    case VK_BACK: A.doc.backspace(); break;
    case VK_DELETE: A.doc.del(); break;
    case VK_F2: A.showGlyphs = !A.showGlyphs; break;           // glyphs <-> plain letters
    default: return;
    }
    refresh();
}

static void onChar(wchar_t ch) {
    if (((GetKeyState(VK_CONTROL) & 0x8000) != 0) && ((GetKeyState(VK_MENU) & 0x8000) == 0)) return;   // Ctrl+letter = command
    if (ch == L'\r') A.doc.insertText(L"\n");
    else if (ch == L'\t') A.doc.insertText(std::wstring(TAB_WIDTH, L' '));
    else if (ch >= 32 && ch != 127) A.doc.insertText(std::wstring(1, ch));
    else return;
    refresh();
}

static void doScroll(bool vertical, WPARAM wp) {
    int w, h; clientSize(w, h);
    int& pos = vertical ? A.scrollY : A.scrollX;
    int line = vertical ? CH() : CW(), page = vertical ? h : w;
    SCROLLINFO si = { sizeof(si), SIF_ALL };
    GetScrollInfo(A.hwnd, vertical ? SB_VERT : SB_HORZ, &si);
    switch (LOWORD(wp)) {
    case SB_LINEUP: pos -= line; break;
    case SB_LINEDOWN: pos += line; break;
    case SB_PAGEUP: pos -= page; break;
    case SB_PAGEDOWN: pos += page; break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    default: return;
    }
    refresh(false);
}

// ----------------------------------------------------------- window ----
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    A.hwnd = hwnd;      // set early: Windows sends WM_SIZE before CreateWindowEx returns
    switch (msg) {
    case WM_CREATE: SetTimer(hwnd, 1, 530, nullptr); return 0;
    case WM_TIMER: A.caretOn = !A.caretOn; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_SIZE: clampScroll(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); paint(dc); EndPaint(hwnd, &ps); return 0; }
    case WM_KEYDOWN: onKeyDown(wp); return 0;
    case WM_CHAR: onChar((wchar_t)wp); return 0;
    case WM_VSCROLL: doScroll(true, wp); return 0;
    case WM_HSCROLL: doScroll(false, wp); return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        if (GetKeyState(VK_SHIFT) & 0x8000) A.scrollX -= d / 120 * CW() * 3; else A.scrollY -= d / 120 * CH() * 3;
        refresh(false); return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd); SetCapture(hwnd); A.dragging = true;
        A.doc.setCaret(posFromPoint((short)LOWORD(lp), (short)HIWORD(lp)), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        refresh(); return 0;
    }
    case WM_MOUSEMOVE:
        if (A.dragging) { A.doc.setCaret(posFromPoint((short)LOWORD(lp), (short)HIWORD(lp)), true); refresh(); }
        return 0;
    case WM_LBUTTONUP: A.dragging = false; ReleaseCapture(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_NEW: if (confirmDiscard()) { A.doc.clear(); A.path.clear(); A.scrollX = A.scrollY = 0; refresh(); } break;
        case ID_OPEN: { if (!confirmDiscard()) break; std::wstring p; if (pickFile(false, p)) openPath(p); break; }
        case ID_SAVE: doSave(false); break;
        case ID_SAVEAS: doSave(true); break;
        case ID_EXIT: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
        case ID_TOGGLE: A.showGlyphs = !A.showGlyphs; refresh(false); break;
        case ID_ABOUT: {
            std::wstring m = APP_TITLE L"\n\nGlyph folder:\n" + A.folder + L"\n\nPictures loaded: " +
                             std::to_wstring(A.bitmaps.size()) + L"\n\nF2 = switch between your glyphs and plain letters.";
            MessageBoxW(hwnd, m.c_str(), APP_TITLE, MB_OK | MB_ICONINFORMATION); break;
        }
        }
        return 0;
    case WM_CLOSE: if (confirmDiscard()) DestroyWindow(hwnd); return 0;
    case WM_DESTROY: KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HMENU buildMenu() {
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), view = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_NEW, L"&New\tCtrl+N");
    AppendMenuW(file, MF_STRING, ID_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, ID_SAVEAS, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_EXIT, L"E&xit");
    AppendMenuW(view, MF_STRING, ID_TOGGLE, L"Glyphs / plain letters\tF2");
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"&About / glyph folder");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    SetProcessDPIAware();
    GdiplusStartupInput gsi; ULONG_PTR token;
    GdiplusStartup(&token, &gsi, nullptr);
    loadGlyphs();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = L"GlyphEditorWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
    RegisterClassW(&wc);
    A.hwnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE, WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
                             CW_USEDEFAULT, CW_USEDEFAULT, START_WIDTH, START_HEIGHT, nullptr, buildMenu(), hInst, nullptr);
    ShowWindow(A.hwnd, nShow);
    if (DEFAULT_TEXT_FILE[0] != 0) openPath(DEFAULT_TEXT_FILE);
    refresh();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }

    for (auto& kv : A.bitmaps) delete kv.second;
    delete A.font;
    GdiplusShutdown(token);
    return (int)m.wParam;
}
