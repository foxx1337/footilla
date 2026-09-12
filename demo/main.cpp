#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>

#include <footilla/editor.h>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>

CAppModule _Module;

namespace {

enum : int {
    EditorId = 100, ExampleId, CompletionId, CallTipId, DarkId, WrapId, ReadOnlyId
};

constexpr const char* Examples[] = {
    "// Now-playing text: functions, metadata and conditional sections\r\n"
    "$if(%isplaying%,\r\n"
    "$if2(\r\n"
    "%album artist%,\r\n"
    "Unknown artist\r\n"
    ") - %title%\r\n"
    "[ '(' %date% ')' ]\r\n"
    "$crlf()\r\n"
    "[%album% / ]$num(%tracknumber%,2)\r\n"
    "[ - %playback_time% / %length%],\r\n"
    "'Playback stopped'\r\n"
    ")\r\n",

    "// Filename generation and nested functions\r\n"
    "$replace(%album artist%,/,_,\\,_)\r\n"
    "\\[%date% - ]%album%\r\n"
    "\\$num(%tracknumber%,2) - %title%\r\n"
    "// Quoted special characters are literal text\r\n"
    "'['%codec%']' 'don''t parse $this or %this%'\r\n",

    "// Try typing $if or %album, then accept with Tab or Enter.\r\n"
    "// Ctrl+Space opens completion; Ctrl+Shift+Space opens a call tip.\r\n"
    "// %datetime% below is an extra field supplied by this host.\r\n"
    "$puts(artist,$if2(%album artist%,%artist%))\r\n"
    "$get(artist) - %title%[ / %album%]\r\n"
    "$crlf()%datetime%\r\n"
};

class MainWindow final : public CWindowImpl<MainWindow>, public CMessageFilter {
public:
    DECLARE_WND_CLASS_EX(L"Footilla.Playground", CS_HREDRAW | CS_VREDRAW, COLOR_WINDOW)

    BOOL PreTranslateMessage(MSG* message) override {
        return IsDialogMessage(message);
    }

    BEGIN_MSG_MAP(MainWindow)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
        MESSAGE_HANDLER(WM_GETMINMAXINFO, OnMinMax)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()

private:
    footilla::Editor editor_;
    CStatic heading_, hint_, legend_, status_;
    CComboBox examples_;
    CButton complete_, callTip_, dark_, wrap_, readOnly_;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;

    int Scale(int size) const { return MulDiv(size, static_cast<int>(dpi_), 96); }

    void UpdateFont() {
        const HFONT old = font_;
        font_ = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Segoe UI");
        if (!font_) {
            throw std::runtime_error("Could not create the demo UI font.");
        }
        const HWND children[] = {heading_, hint_, legend_, status_, examples_,
            complete_, callTip_, dark_, wrap_, readOnly_};
        for (HWND child : children) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        if (old) {
            DeleteObject(old);
        }
    }

    void Layout() {
        RECT rc{};
        GetClientRect(&rc);
        const int margin = Scale(18);
        const int width = (std::max)(0L, rc.right - 2 * margin);
        heading_.MoveWindow(margin, Scale(12), width, Scale(25));
        hint_.MoveWindow(margin, Scale(40), width, Scale(22));
        examples_.MoveWindow(margin, Scale(74), Scale(236), Scale(240));
        complete_.MoveWindow(Scale(266), Scale(74), Scale(105), Scale(28));
        callTip_.MoveWindow(Scale(379), Scale(74), Scale(110), Scale(28));
        dark_.MoveWindow(Scale(510), Scale(76), Scale(74), Scale(24));
        wrap_.MoveWindow(Scale(588), Scale(76), Scale(76), Scale(24));
        readOnly_.MoveWindow(Scale(668), Scale(76), Scale(110), Scale(24));
        legend_.MoveWindow(margin, Scale(113), width, Scale(22));
        ::MoveWindow(editor_.Handle(), margin, Scale(144), width,
            (std::max)(Scale(80), static_cast<int>(rc.bottom) - Scale(185)), TRUE);
        status_.MoveWindow(margin, rc.bottom - Scale(31), width, Scale(23));
    }

    void UpdateStatus() {
        const auto text = editor_.GetText();
        const std::wstring message = std::to_wstring(text.size()) +
            L" UTF-8 bytes  |  Visual indentation adds no spaces  |  No foobar2000 evaluation";
        status_.SetWindowText(message.c_str());
    }

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
        try {
            dpi_ = GetDpiForWindow(m_hWnd);
            RECT empty{};
            heading_.Create(m_hWnd, empty, L"Footilla  /  foobar2000 title-format editor",
                WS_CHILD | WS_VISIBLE);
            hint_.Create(m_hWnd, empty,
                L"Type $ or % for suggestions. Tab / Enter accepts; Esc dismisses. Ctrl+Space reopens.",
                WS_CHILD | WS_VISIBLE);
            examples_.Create(m_hWnd, empty, nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, ExampleId);
            examples_.AddString(L"Now playing");
            examples_.AddString(L"Filename and quoting");
            examples_.AddString(L"Variables and custom fields");
            examples_.SetCurSel(0);
            complete_.Create(m_hWnd, empty, L"Complete", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, CompletionId);
            callTip_.Create(m_hWnd, empty, L"Function help", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, CallTipId);
            dark_.Create(m_hWnd, empty, L"Dark", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, DarkId);
            wrap_.Create(m_hWnd, empty, L"Wrap", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, WrapId);
            readOnly_.Create(m_hWnd, empty, L"Read only", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, ReadOnlyId);
            legend_.Create(m_hWnd, empty,
                L"Fields: teal   Functions: purple   Quoted literals: orange   Comments: green   Conditionals: gold",
                WS_CHILD | WS_VISIBLE);
            status_.Create(m_hWnd, empty, L"", WS_CHILD | WS_VISIBLE);
            if (!heading_ || !hint_ || !examples_ || !complete_ || !callTip_ ||
                !dark_ || !wrap_ || !readOnly_ || !legend_ || !status_ ||
                !editor_.Create(m_hWnd, EditorId, empty)) {
                throw std::runtime_error("Could not create the Footilla demo controls.");
            }
            editor_.SetExtraFields({"datetime"});
            editor_.SetText(Examples[0]);
            UpdateFont();
            Layout();
            UpdateStatus();
            return 0;
        } catch (const std::exception& error) {
            MessageBoxA(m_hWnd, error.what(), "Footilla demo", MB_OK | MB_ICONERROR);
            return -1;
        }
    }

    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&) {
        Layout();
        return 0;
    }

    LRESULT OnDpiChanged(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
        dpi_ = HIWORD(wParam);
        try {
            UpdateFont();
        } catch (const std::exception& error) {
            MessageBoxA(m_hWnd, error.what(), "Footilla demo", MB_OK | MB_ICONERROR);
        }
        const auto* bounds = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(nullptr, bounds, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    LRESULT OnMinMax(UINT, WPARAM, LPARAM lParam, BOOL&) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize = {Scale(830), Scale(440)};
        return 0;
    }

    LRESULT OnCommand(UINT, WPARAM wParam, LPARAM, BOOL& handled) {
        try {
            switch (LOWORD(wParam)) {
            case EditorId:
                if (HIWORD(wParam) == EN_CHANGE) {
                    UpdateStatus();
                }
                break;
            case ExampleId:
                if (HIWORD(wParam) == CBN_SELCHANGE) {
                    editor_.SetText(Examples[examples_.GetCurSel()]);
                    editor_.Focus();
                }
                break;
            case CompletionId:
                editor_.Focus();
                editor_.ShowCompletion();
                break;
            case CallTipId:
                editor_.Focus();
                editor_.ShowCallTip();
                break;
            case DarkId:
                editor_.SetTheme(dark_.GetCheck() ? footilla::Theme::Dark : footilla::Theme::Light);
                break;
            case WrapId:
                editor_.SetWordWrap(wrap_.GetCheck() != 0);
                break;
            case ReadOnlyId:
                editor_.SetReadOnly(readOnly_.GetCheck() != 0);
                break;
            default:
                handled = FALSE;
                break;
            }
        } catch (const std::exception& error) {
            MessageBoxA(m_hWnd, error.what(), "Footilla demo", MB_OK | MB_ICONERROR);
        }
        return 0;
    }

    LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&) {
        if (editor_.Handle()) {
            editor_.Focus();
        }
        return 0;
    }

    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
        editor_.Destroy();
        if (font_) {
            DeleteObject(font_);
            font_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
};

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) {
        MessageBoxW(nullptr, L"OLE initialization failed.", L"Footilla demo", MB_ICONERROR);
        return 1;
    }
    if (FAILED(_Module.Init(nullptr, instance))) {
        OleUninitialize();
        return 1;
    }
    if (!footilla::Initialize(instance)) {
        const auto message = L"Footilla initialization failed. Windows error " + std::to_wstring(GetLastError());
        MessageBoxW(nullptr, message.c_str(), L"Footilla demo", MB_ICONERROR);
        _Module.Term();
        OleUninitialize();
        return 1;
    }

    CMessageLoop loop;
    _Module.AddMessageLoop(&loop);
    int result = 1;
    {
        MainWindow window;
        RECT bounds{100, 100, 1150, 820};
        if (window.Create(nullptr, bounds, L"Footilla - Title Formatting Playground",
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN)) {
            loop.AddMessageFilter(&window);
            window.ShowWindow(show);
            window.UpdateWindow();
            result = loop.Run();
            loop.RemoveMessageFilter(&window);
        }
    }
    _Module.RemoveMessageLoop();
    if (!footilla::Shutdown()) {
        MessageBoxW(nullptr, L"Footilla resource shutdown failed.", L"Footilla demo", MB_ICONERROR);
        result = 1;
    }
    _Module.Term();
    OleUninitialize();
    return result;
}
