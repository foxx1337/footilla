#include <footilla/editor.h>
#include <Scintilla.h>
#include <ScintillaFootilla.h>
#include <ole2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

LRESULT Send(HWND window, UINT message, WPARAM w = 0, LPARAM l = 0) {
    return SendMessageW(window, message, w, l);
}

void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

class Image {
public:
    explicit Image(HWND window) {
        RECT bounds{};
        Check(GetClientRect(window, &bounds) != FALSE, "Get client size");
        width = bounds.right;
        height = bounds.bottom;
        dc = CreateCompatibleDC(nullptr);
        Check(dc != nullptr, "Create render DC");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap) {
            DeleteDC(dc);
            throw std::runtime_error("Create render bitmap");
        }
        previous = SelectObject(dc, bitmap);
        Send(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        GdiFlush();
    }
    ~Image() {
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    COLORREF Pixel(int x, int y) const {
        Check(x >= 0 && x < width && y >= 0 && y < height, "Pixel outside capture");
        const auto pixel = static_cast<const std::uint32_t*>(pixels)[y * width + x];
        return RGB((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255);
    }
    void Save(const std::filesystem::path& path) const {
        BITMAPFILEHEADER file{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
        file.bfSize = file.bfOffBits + width * height * 4;
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = width;
        info.biHeight = -height;
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&info), sizeof(info));
        output.write(static_cast<const char*>(pixels), width * height * 4);
        Check(output.good(), "Write render snapshot");
    }
    int width = 0;
    int height = 0;
private:
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    void* pixels = nullptr;
};

constexpr const char* Sample =
    "$if(%artist%,\r\n"
    "$if2(\r\n"
    "%title%,Unknown\r\n"
    "),\r\n"
    "No artist\r\n"
    ")";

class Fixture {
public:
    Fixture() {
        parent = CreateWindowExW(0, L"STATIC", L"Visual inset tests", WS_OVERLAPPEDWINDOW,
            0, 0, 900, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(parent != nullptr, "Create test parent");
        const RECT bounds{0, 0, 800, 500};
        if (!editor.Create(parent, 100, bounds)) {
            DestroyWindow(parent);
            throw std::runtime_error("Create visual editor");
        }
        sci = editor.ScintillaHandle();
        Send(sci, SCI_SETCARETWIDTH, 0);
        Send(sci, SCI_SETLAYOUTCACHE, SC_CACHE_DOCUMENT);
    }
    ~Fixture() { DestroyWindow(parent); }
    void Load(const std::string& text) {
        editor.SetText(text);
        Send(sci, SCI_GOTOPOS, 0);
        Send(sci, SCI_SETXOFFSET, 0);
        Send(sci, SCI_SETFIRSTVISIBLELINE, 0);
        Render();
    }
    void Render() { Pump(); const Image image(sci); }
    LRESULT Start(int line) { return Send(sci, SCI_POSITIONFROMLINE, line); }
    int X(LRESULT position) { return static_cast<int>(Send(sci, SCI_POINTXFROMPOSITION, 0, position)); }
    int Y(LRESULT position) { return static_cast<int>(Send(sci, SCI_POINTYFROMPOSITION, 0, position)); }
    int Inset(int line) { return static_cast<int>(Send(sci, Scintilla::Footilla::GetLineInset, line)); }
    void RoundTrip(LRESULT position) {
        const auto hit = Send(sci, SCI_POSITIONFROMPOINT, X(position), Y(position) + 2);
        Check(hit == position, "Position/point round trip");
    }
    footilla::Editor editor;
    HWND parent = nullptr;
    HWND sci = nullptr;
};

void GeometryTests(Fixture& f) {
    Check(f.editor.GetVisualIndentationWidth() == 2, "Default visual width");
    f.Load(Sample);
    Check(f.editor.GetText() == Sample, "Rendering must not change document bytes");
    const int expected[] = {0, 2, 4, 2, 2, 0};
    for (int line = 0; line < 6; ++line) {
        Check(f.Inset(line) == expected[line], "Expected syntax indentation");
        f.RoundTrip(f.Start(line));
        f.RoundTrip(f.Start(line) + 1);
    }
    const int base = f.X(0);
    const int step = f.X(f.Start(1)) - base;
    const auto twoSpaces = Send(f.sci, SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("  "));
    Check(std::abs(step - twoSpaces) <= 1, "Default inset equals two rendered spaces");
    Check(step > 0 && std::abs(f.X(f.Start(2)) - base - 2 * step) <= 1, "Nested visual offsets");
    Check(f.X(f.Start(3)) == f.X(f.Start(1)), "Closing delimiter must align with opener");
    const auto modified = Send(f.sci, SCI_GETMODIFY);
    Send(f.sci, SCI_SETSEL, f.Start(1), f.Start(4));
    const auto anchor = Send(f.sci, SCI_GETANCHOR);
    const auto caret = Send(f.sci, SCI_GETCURRENTPOS);
    f.editor.SetVisualIndentationWidth(4);
    Check(Send(f.sci, SCI_GETMODIFY) == modified && !Send(f.sci, SCI_CANUNDO), "Display settings must not alter undo/modified state");
    Check(Send(f.sci, SCI_GETANCHOR) == anchor && Send(f.sci, SCI_GETCURRENTPOS) == caret, "Display settings preserve selection");
    Check(std::abs(f.X(f.Start(1)) - base - 2 * step) <= 1, "Configurable visual width");
    f.editor.SetVisualIndentationWidth(0);
    for (int line = 0; line < 6; ++line) {
        Check(f.X(f.Start(line)) == base, "Zero width disables visual insets");
    }
    f.editor.SetVisualIndentationWidth(2);
    bool rejected = false;
    try {
        f.editor.SetVisualIndentationWidth(-1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected && f.editor.GetVisualIndentationWidth() == 2, "Reject invalid visual width without changing settings");
    Send(f.sci, SCI_SETSEL, 0, -1);
    const auto length = Send(f.sci, SCI_GETSELTEXT);
    std::string selected(static_cast<std::size_t>(length) + 1, '\0');
    Send(f.sci, SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(selected.data()));
    selected.resize(static_cast<std::size_t>(length));
    Check(selected == Sample, "Selected text must contain no synthetic whitespace");
    Send(f.sci, SCI_GOTOPOS, f.Start(2) + 3);
    Send(f.sci, SCI_HOME);
    Check(Send(f.sci, SCI_GETCURRENTPOS) == f.Start(2), "Home goes to first real character");
    Send(f.sci, SCI_CHARLEFT);
    Check(Send(f.sci, SCI_GETCURRENTPOS) == Send(f.sci, SCI_GETLINEENDPOSITION, 1), "Left traverses newline, not inset");
    Send(f.sci, SCI_CHARRIGHT);
    Check(Send(f.sci, SCI_GETCURRENTPOS) == f.Start(2), "Right skips inset without materializing it");
    Send(f.sci, SCI_SETVIRTUALSPACEOPTIONS, SCVS_USERACCESSIBLE);
    const int x = base + 2;
    const int y = f.Y(f.Start(2)) + 2;
    Check(Send(f.sci, SCI_POSITIONFROMPOINT, x, y) == f.Start(2), "Clicking visual padding maps to line start");
    Check(Send(f.sci, SCI_POSITIONFROMPOINTCLOSE, x, y) == -1, "Exact hit query rejects nontext padding");
    Send(f.sci, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    Send(f.sci, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    Check(Send(f.sci, SCI_GETCURRENTPOS) == f.Start(2) &&
        Send(f.sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0) == 0, "Padding click must not enter virtual space");
    Send(f.sci, SCI_SETVIRTUALSPACEOPTIONS, SCVS_NONE);
    Send(f.sci, SCI_GOTOPOS, f.Start(2));
    Send(f.sci, SCI_LINEDOWN);
    Check(Send(f.sci, SCI_LINEFROMPOSITION, Send(f.sci, SCI_GETCURRENTPOS)) == 3, "Down arrow follows display coordinates");
    Send(f.sci, SCI_LINEUP);
    Check(Send(f.sci, SCI_GETCURRENTPOS) == f.Start(2), "Up/down preserve visual caret column");
    Send(f.sci, SCI_SETRECTANGULARSELECTIONANCHOR, f.Start(1) + 2);
    Send(f.sci, SCI_SETRECTANGULARSELECTIONCARET, f.Start(2) + 2);
    const auto selections = Send(f.sci, SCI_GETSELECTIONS);
    Check(selections == 2, "Rectangular selection spans two document lines");
    for (LRESULT i = 0; i < selections; ++i) {
        const auto position = Send(f.sci, SCI_GETSELECTIONNCARET, i);
        f.RoundTrip(position);
    }
    Send(f.sci, SCI_SETSELECTIONMODE, SC_SEL_STREAM);
    Send(f.sci, SCI_CLEARSELECTIONS);
    Send(f.sci, SCI_GOTOPOS, 0);
}

void EditingTests(Fixture& f) {
    for (const int eol : {SC_EOL_CRLF, SC_EOL_LF, SC_EOL_CR}) {
        Send(f.sci, SCI_SETEOLMODE, eol);
        f.Load("$if2(");
        Send(f.sci, SCI_GOTOPOS, 5);
        Send(f.sci, WM_KEYDOWN, VK_RETURN, 1);
        Send(f.sci, WM_CHAR, '\r', 1);
        const std::string newline = eol == SC_EOL_CRLF ? "\r\n" : eol == SC_EOL_LF ? "\n" : "\r";
        Check(f.editor.GetText() == "$if2(" + newline, "Enter must insert only newline");
        Check(f.Inset(1) == 2 && f.X(f.Start(1)) > f.X(0), "Empty line has a visual inset immediately");
        Send(f.sci, SCI_NEWLINE);
        Check(f.editor.GetText() == "$if2(" + newline + newline && f.Inset(2) == 2, "Repeated Enter keeps visual level without bytes");
        Send(f.sci, WM_CHAR, ')', 1);
        Check(f.Inset(2) == 0, "Typing leading closer dedents its line");
        Send(f.sci, SCI_NEWLINE);
        Check(f.Inset(3) == 0, "Line after closer returns to outer level");
    }
    Send(f.sci, SCI_SETEOLMODE, SC_EOL_CRLF);
    f.Load(Sample);
    Send(f.sci, SCI_SETTARGETRANGE, 0, f.Start(1));
    Send(f.sci, SCI_REPLACETARGET, 0, reinterpret_cast<LPARAM>(""));
    Check(f.Inset(1) == 2, "Removing outer opener reindents following lines");
    Send(f.sci, SCI_UNDO);
    Check(f.editor.GetText() == Sample && f.Inset(2) == 4, "Undo restores syntax insets");
    Send(f.sci, SCI_REDO);
    Check(f.Inset(1) == 2, "Redo recomputes syntax insets");
    f.Load("$if2(\r\n");
    Send(f.sci, SCI_GOTOPOS, f.editor.GetText().size());
    Send(f.sci, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("%artist%,\r\n  literal space\r\n)"));
    Check(f.editor.GetText() == "$if2(\r\n%artist%,\r\n  literal space\r\n)", "Bulk paste keeps literal whitespace exactly");
    Check(f.Inset(2) == 2 && f.Inset(3) == 0, "Paste reindents full document");
    f.Load("$if2(\r\n%alb");
    Send(f.sci, SCI_GOTOPOS, f.editor.GetText().size());
    f.editor.ShowCompletion();
    Check(Send(f.sci, SCI_AUTOCACTIVE) != 0, "Completion on visually indented line");
    Send(f.sci, SCI_AUTOCSELECT, 0, reinterpret_cast<LPARAM>("%album%"));
    Send(f.sci, SCI_AUTOCCOMPLETE);
    Check(f.editor.GetText() == "$if2(\r\n%album%" && f.Inset(1) == 2, "Completion preserves byte positions");
    f.editor.ShowCallTip();
    Check(Send(f.sci, SCI_CALLTIPACTIVE) != 0, "Call tip in indented argument");
    Send(f.sci, SCI_CALLTIPCANCEL);
    f.editor.SetReadOnly(true);
    const auto before = f.editor.GetText();
    f.editor.SetVisualIndentationWidth(4);
    Check(f.editor.GetText() == before && Send(f.sci, SCI_GETREADONLY), "Read-only visual setting");
    f.editor.SetReadOnly(false);
    f.editor.SetVisualIndentationWidth(2);
}

void PaintTests(Fixture& f, const std::filesystem::path& snapshot) {
    f.Load(Sample);
    const int base = f.X(0);
    for (auto theme : {footilla::Theme::Light, footilla::Theme::Dark}) {
        f.editor.SetTheme(theme);
        for (int phase : {SC_PHASES_ONE, SC_PHASES_TWO, SC_PHASES_MULTIPLE}) {
            Send(f.sci, SCI_SETPHASESDRAW, phase);
            Send(f.sci, SCI_SETBUFFEREDDRAW, phase != SC_PHASES_MULTIPLE);
            Send(f.sci, SCI_SETSEL, 0, -1);
            const Image image(f.sci);
            const COLORREF gutter = static_cast<COLORREF>(Send(f.sci, SCI_STYLEGETBACK, STYLE_LINENUMBER));
            Check(image.Pixel(base + 3, f.Y(f.Start(2)) + 3) == gutter, "Jagged inset background survives selected text and paint phases");
            Send(f.sci, SCI_GOTOPOS, 0);
            const Image unselected(f.sci);
            bool foundInk = false;
            const COLORREF background = static_cast<COLORREF>(Send(f.sci, SCI_STYLEGETBACK, STYLE_DEFAULT));
            const int start = f.X(f.Start(2));
            const int top = f.Y(f.Start(2));
            const int height = static_cast<int>(Send(f.sci, SCI_TEXTHEIGHT, 2));
            for (int y = top; y < top + height; ++y) {
                for (int x = start; x < start + 70; ++x) {
                    if (unselected.Pixel(x, y) != background) {
                        foundInk = true;
                    }
                }
            }
            Check(foundInk, "Indented text must actually be painted");
        }
    }
    f.editor.SetTheme(footilla::Theme::Light);
    Send(f.sci, SCI_SETBUFFEREDDRAW, true);
    Send(f.sci, SCI_SETPHASESDRAW, SC_PHASES_TWO);
    f.Load("$if2(\r\n\r\n)");
    Send(f.sci, SCI_SETFOCUS, true);
    Send(f.sci, SCI_SETCARETPERIOD, 0);
    Send(f.sci, SCI_SETCARETWIDTH, 2);
    Send(f.sci, SCI_GOTOPOS, f.Start(1));
    const COLORREF caretColour = static_cast<COLORREF>(Send(f.sci, SCI_GETCARETFORE));
    const Image caretImage(f.sci);
    const int caretX = f.X(f.Start(1));
    const int caretY = f.Y(f.Start(1)) + 3;
    Check(caretImage.Pixel(caretX, caretY) == caretColour ||
        caretImage.Pixel(caretX - 1, caretY) == caretColour, "Caret is painted at the inset on an empty line");
    Send(f.sci, SCI_SETCARETWIDTH, 0);
    Send(f.sci, SCI_SETFOCUS, false);
    f.Load(Sample);
    if (!snapshot.empty()) {
        const Image image(f.sci);
        image.Save(snapshot);
    }
}

void WrappingTests(Fixture& f) {
    f.Load(Sample);
    Send(f.sci, SCI_SETXOFFSET, 10);
    Check(f.X(f.Start(2)) > f.X(0), "Horizontal scroll preserves inset delta");
    f.RoundTrip(f.Start(2) + 2);
    Send(f.sci, SCI_SETXOFFSET, 0);
    const int initialStep = f.X(f.Start(1)) - f.X(0);
    Send(f.sci, SCI_SETZOOM, 5);
    f.Render();
    Check(f.X(f.Start(1)) - f.X(0) > initialStep, "Inset scales with zoom");
    f.RoundTrip(f.Start(2));
    Send(f.sci, SCI_SETZOOM, 0);
    f.editor.SetFont("Consolas", 16);
    f.Render();
    Check(f.X(f.Start(1)) - f.X(0) > initialStep, "Inset scales with font size");
    f.editor.SetFont("Consolas", 11);
    f.editor.SetLineNumbers(false);
    f.RoundTrip(f.Start(2));
    f.editor.SetLineNumbers(true);
    std::string manyLines = "$if2(\r\n";
    for (int i = 0; i < 50; ++i) manyLines += "%title%\r\n";
    manyLines += ")";
    f.Load(manyLines);
    Send(f.sci, SCI_SETFIRSTVISIBLELINE, 20);
    f.Render();
    Check(f.Y(f.Start(20)) == 0 && f.Inset(20) == 2, "Vertical scrolling preserves per-document-line inset");
    f.RoundTrip(f.Start(20) + 1);
    const std::string longLine(140, 'x');
    f.Load("$if2(\r\n" + longLine + "\r\n)");
    MoveWindow(f.editor.Handle(), 0, 0, 240, 500, TRUE);
    f.editor.SetWordWrap(true);
    f.Render();
    Check(Send(f.sci, SCI_WRAPCOUNT, 1) > 1, "Long indented line wraps");
    const auto defaultWrapCount = Send(f.sci, SCI_WRAPCOUNT, 1);
    const auto start = f.Start(1);
    const int firstY = f.Y(start);
    LRESULT continuation = start;
    while (continuation < start + static_cast<LRESULT>(longLine.size()) && f.Y(continuation) == firstY) {
        ++continuation;
    }
    Check(f.X(continuation) == f.X(start), "Continuation retains visual inset");
    f.RoundTrip(continuation + 1);
    for (LRESULT p = start; p < start + static_cast<LRESULT>(longLine.size()); ++p) {
        f.RoundTrip(p);
    }
    f.editor.SetVisualIndentationWidth(20);
    f.Render();
    Check(Send(f.sci, SCI_WRAPCOUNT, 1) > defaultWrapCount, "Inset reduces available wrap width");
    f.RoundTrip(start);
    f.RoundTrip(start + 1);
    Check(f.X(start) < 240, "Deep wrapped indentation leaves text visible");
    f.editor.SetVisualIndentationWidth(2);
    MoveWindow(f.editor.Handle(), 0, 0, 800, 500, TRUE);
    f.editor.SetWordWrap(false);
    f.Load(u8"$if2(\r\n%artist% - caf\u00e9\r\n)");
    f.RoundTrip(f.Start(1));
    f.RoundTrip(f.Start(1) + 11);
    f.Load("$if2(\r\n" + longLine + "\r\n)");
    Check(Send(f.sci, SCI_GETSCROLLWIDTH) >= f.X(f.Start(1) + longLine.size()) - f.X(0),
        "Horizontal extent includes the visual inset");
}

void IsolationTests(Fixture& f) {
    footilla::Options options;
    options.visualIndentationWidth = 0;
    footilla::Editor other;
    const RECT bounds{0, 0, 600, 300};
    Check(other.Create(f.parent, 200, bounds, options), "Create second editor with visual indentation disabled");
    other.SetText(Sample);
    f.Load(Sample);
    Check(Send(other.ScintillaHandle(), Scintilla::Footilla::GetLineInset, 2) == 0 &&
        f.Inset(2) == 4, "Insets are isolated between editor instances");
    other.SetVisualIndentationWidth(3);
    Check(Send(other.ScintillaHandle(), Scintilla::Footilla::GetLineInset, 2) == 6 &&
        f.Inset(2) == 4, "Updating one editor cannot affect another");
    const int invalid[] = {-1};
    Check(!Send(f.sci, Scintilla::Footilla::SetLineInsets, 1, reinterpret_cast<LPARAM>(invalid)) &&
        Send(f.sci, SCI_GETSTATUS) == SC_STATUS_FAILURE && f.Inset(2) == 4,
        "Invalid engine inset arrays report failure and preserve state");
    Send(f.sci, SCI_SETSTATUS, SC_STATUS_OK);
    std::vector<int> invalidColumns(6, 0);
    invalidColumns[2] = -1;
    Check(!Send(f.sci, Scintilla::Footilla::SetLineInsets, invalidColumns.size(),
        reinterpret_cast<LPARAM>(invalidColumns.data())) && f.Inset(2) == 4 &&
        Send(f.sci, SCI_GETSTATUS) == SC_STATUS_FAILURE, "Reject negative engine inset columns");
    Send(f.sci, SCI_SETSTATUS, SC_STATUS_OK);
    other.Destroy();
    options.visualIndentationWidth = -1;
    Check(!other.Create(f.parent, 200, bounds, options), "Reject negative creation-time visual width");
}

void DirectWriteTests(Fixture& f) {
    Send(f.sci, SCI_SETTECHNOLOGY, SC_TECHNOLOGY_DIRECTWRITE);
    Check(Send(f.sci, SCI_GETTECHNOLOGY) == SC_TECHNOLOGY_DIRECTWRITE, "Initialize DirectWrite renderer");
    for (const int bidi : {SC_BIDIRECTIONAL_DISABLED, SC_BIDIRECTIONAL_L2R}) {
        Send(f.sci, SCI_SETBIDIRECTIONAL, bidi);
        f.Load(Sample);
        Check(f.X(f.Start(2)) > f.X(f.Start(1)), "DirectWrite applies nesting insets");
        for (int line = 0; line < 6; ++line) {
            f.RoundTrip(f.Start(line));
            f.RoundTrip(f.Start(line) + 1);
        }
        MoveWindow(f.editor.Handle(), 0, 0, 300, 500, TRUE);
        f.editor.SetWordWrap(true);
        const std::string content = "$if2(\r\n" + std::string(100, 'x') + "\r\n)";
        f.Load(content);
        Check(Send(f.sci, SCI_WRAPCOUNT, 1) > 1, "DirectWrite wraps inset line");
        for (LRESULT p = f.Start(1); p < f.Start(1) + 100; ++p) {
            f.RoundTrip(p);
        }
        f.editor.SetWordWrap(false);
        MoveWindow(f.editor.Handle(), 0, 0, 800, 500, TRUE);
    }
    Send(f.sci, SCI_SETBIDIRECTIONAL, SC_BIDIRECTIONAL_DISABLED);
    Send(f.sci, SCI_SETTECHNOLOGY, SC_TECHNOLOGY_DEFAULT);
}

}

int wmain(int argc, wchar_t** argv) {
    const auto ole = OleInitialize(nullptr);
    bool initialized = false;
    try {
        Check(SUCCEEDED(ole), "Initialize OLE");
        Check(footilla::Initialize(GetModuleHandleW(nullptr)), "Initialize Footilla");
        initialized = true;
        {
            Fixture f;
            GeometryTests(f);
            EditingTests(f);
            PaintTests(f, argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
            WrappingTests(f);
            IsolationTests(f);
            DirectWriteTests(f);
        }
        Check(footilla::Shutdown(), "Shutdown Footilla");
        initialized = false;
        OleUninitialize();
        std::cout << "All visual inset tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (Windows error " << GetLastError() << ")\n";
        if (initialized) footilla::Shutdown();
        if (SUCCEEDED(ole)) OleUninitialize();
        return 1;
    }
}
