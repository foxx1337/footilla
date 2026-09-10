#include <footilla/editor.h>
#include <footilla/language.h>

#include <Scintilla.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace footilla {
namespace {

constexpr wchar_t WindowClass[] = L"Footilla.Editor";
constexpr UINT ChangedMessage = WM_APP + 0x341;
HINSTANCE instance = nullptr;
DWORD uiThread = 0;
unsigned int liveEditors = 0;

bool IsUiThread() {
    return instance && GetCurrentThreadId() == uiThread;
}

void ValidateUtf8(std::string_view text) {
    if (text.find('\0') != std::string_view::npos ||
        text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        (!text.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0))) {
        throw std::invalid_argument("Footilla expects valid UTF-8 without embedded NUL.");
    }
}

}

bool Initialize(HINSTANCE module) {
    if (instance) {
        if (module == instance && IsUiThread()) {
            return true;
        }
        SetLastError(ERROR_INVALID_STATE);
        return false;
    }
    if (!module) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        SetLastError(ERROR_DLL_INIT_FAILED);
        return false;
    }
    if (!Scintilla_RegisterClasses(module)) {
        return false;
    }
    instance = module;
    uiThread = GetCurrentThreadId();
    return true;
}

bool Shutdown() {
    if (!IsUiThread() || liveEditors) {
        SetLastError(liveEditors ? ERROR_BUSY : ERROR_INVALID_STATE);
        return false;
    }
    WNDCLASSEXW registered{sizeof(registered)};
    if (GetClassInfoExW(instance, WindowClass, &registered) &&
        !UnregisterClassW(WindowClass, instance)) {
        return false;
    }
    if (!Scintilla_ReleaseResources()) {
        return false;
    }
    instance = nullptr;
    uiThread = 0;
    return true;
}

Editor::~Editor() {
    Destroy();
}

bool Editor::Create(HWND parent, int controlId, const RECT& bounds, const Options& options) {
    if (!IsUiThread() || window_ || !IsWindow(parent) ||
        GetWindowThreadProcessId(parent, nullptr) != uiThread ||
        controlId < 0 || controlId > 65535 ||
        options.fontSizePoints < 1 || options.fontSizePoints > 200) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    WNDCLASSEXW existing{sizeof(existing)};
    if (!GetClassInfoExW(instance, WindowClass, &existing)) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        wc.lpszClassName = WindowClass;
        if (!RegisterClassExW(&wc)) {
            return false;
        }
    }
    if (!CreateWindowExW(WS_EX_CONTROLPARENT, WindowClass, L"Title formatting editor",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), instance, this)) {
        return false;
    }
    Send(SCI_SETCODEPAGE, SC_CP_UTF8);
    Send(SCI_SETILEXER, 0, 0);
    Send(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
    Send(SCI_SETEOLMODE, SC_EOL_CRLF);
    Send(SCI_SETTABWIDTH, 4);
    Send(SCI_SETUSETABS, false);
    Send(SCI_SETSCROLLWIDTH, 1);
    Send(SCI_SETSCROLLWIDTHTRACKING, true);
    Send(SCI_AUTOCSETSEPARATOR, '\n');
    Send(SCI_AUTOCSETTYPESEPARATOR, '\t');
    Send(SCI_AUTOCSETIGNORECASE, true);
    Send(SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT);
    Send(SCI_AUTOCSETMAXHEIGHT, 10);
    Send(SCI_AUTOCSETMAXWIDTH, 48);
    Send(SCI_AUTOCSETCHOOSESINGLE, false);
    Send(SCI_AUTOCSETCANCELATSTART, false);
    Send(SCI_AUTOCSETFILLUPS, 0, reinterpret_cast<LPARAM>(""));
    SetFont("Consolas", options.fontSizePoints);
    SetTheme(options.theme);
    SetLineNumbers(options.lineNumbers);
    SetWordWrap(options.wordWrap);
    SetReadOnly(options.readOnly);
    return true;
}

void Editor::Destroy() {
    if (window_ && !DestroyWindow(window_)) {
        // A live HWND cannot safely retain a pointer to a destroyed C++ object.
        std::terminate();
    }
}

void Editor::RequireWindow() const {
    if (!window_ || !scintilla_ || !IsUiThread()) {
        throw std::logic_error("Footilla editor must be created and used on its UI thread.");
    }
}

std::intptr_t Editor::Send(unsigned int message, std::uintptr_t wParam, std::intptr_t lParam) const {
    return SendMessageW(scintilla_, message, wParam, lParam);
}

void Editor::SetText(std::string_view text) {
    RequireWindow();
    ValidateUtf8(text);
    const std::string terminated(text);
    const auto readOnly = Send(SCI_GETREADONLY);
    Send(SCI_SETREADONLY, false);
    Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(terminated.c_str()));
    Send(SCI_SETREADONLY, readOnly);
    Send(SCI_EMPTYUNDOBUFFER);
    Send(SCI_SETSAVEPOINT);
    styleDirty_ = true;
    Restyle();
}

std::string Editor::GetText() const {
    RequireWindow();
    const auto length = static_cast<std::size_t>(Send(SCI_GETLENGTH));
    std::string text(length + 1, '\0');
    Send(SCI_GETTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
    text.resize(length);
    return text;
}

void Editor::SetReadOnly(bool readOnly) {
    RequireWindow();
    Send(SCI_SETREADONLY, readOnly);
    if (readOnly) {
        Send(SCI_AUTOCCANCEL);
        Send(SCI_CALLTIPCANCEL);
    }
}

void Editor::SetTheme(Theme theme) {
    RequireWindow();
    const bool dark = theme == Theme::Dark;
    const COLORREF back = dark ? RGB(30, 32, 38) : RGB(255, 255, 255);
    const COLORREF fore = dark ? RGB(220, 224, 232) : RGB(35, 40, 48);
    Send(SCI_STYLESETFORE, STYLE_DEFAULT, fore);
    Send(SCI_STYLESETBACK, STYLE_DEFAULT, back);
    Send(SCI_STYLECLEARALL);
    const auto style = [this](language::Style token, COLORREF color, bool bold = false) {
        Send(SCI_STYLESETFORE, static_cast<unsigned int>(token), color);
        Send(SCI_STYLESETBOLD, static_cast<unsigned int>(token), bold);
    };
    style(language::Style::Field, dark ? RGB(100, 204, 220) : RGB(0, 112, 128));
    style(language::Style::Function, dark ? RGB(199, 158, 255) : RGB(119, 44, 170), true);
    style(language::Style::Quoted, dark ? RGB(235, 184, 126) : RGB(163, 81, 25));
    style(language::Style::Comment, dark ? RGB(137, 163, 119) : RGB(79, 120, 58));
    style(language::Style::Operator, fore, true);
    style(language::Style::Conditional, dark ? RGB(249, 204, 107) : RGB(150, 104, 0), true);
    style(language::Style::Number, dark ? RGB(163, 204, 152) : RGB(46, 123, 59));
    style(language::Style::Error, dark ? RGB(255, 122, 136) : RGB(196, 35, 55));
    Send(SCI_STYLESETITALIC, static_cast<unsigned int>(language::Style::Comment), true);
    Send(SCI_STYLESETFORE, STYLE_LINENUMBER, dark ? RGB(140, 147, 161) : RGB(108, 116, 128));
    Send(SCI_STYLESETBACK, STYLE_LINENUMBER, dark ? RGB(38, 41, 49) : RGB(242, 244, 248));
    Send(SCI_SETCARETFORE, fore);
    Send(SCI_SETSELBACK, true, dark ? RGB(61, 77, 110) : RGB(201, 222, 250));
    Send(SCI_STYLESETBACK, STYLE_BRACELIGHT, dark ? RGB(67, 84, 103) : RGB(212, 231, 250));
    Send(SCI_STYLESETFORE, STYLE_BRACELIGHT, fore);
    Send(SCI_STYLESETFORE, STYLE_BRACEBAD, RGB(220, 40, 65));
    Send(SCI_CALLTIPUSESTYLE, 16);
    Send(SCI_CALLTIPSETBACK, back);
    Send(SCI_CALLTIPSETFORE, fore);
    Send(SCI_CALLTIPSETFOREHLT, dark ? RGB(249, 204, 107) : RGB(119, 44, 170));
    styleDirty_ = true;
    Restyle();
}

void Editor::SetWordWrap(bool enabled) {
    RequireWindow();
    Send(SCI_SETWRAPMODE, enabled ? SC_WRAP_WORD : SC_WRAP_NONE);
}

void Editor::SetLineNumbers(bool enabled) {
    RequireWindow();
    lineNumbers_ = enabled;
    Send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    Send(SCI_SETMARGINWIDTHN, 0, enabled ? Send(SCI_TEXTWIDTH, STYLE_LINENUMBER,
        reinterpret_cast<LPARAM>("_99999")) : 0);
    Send(SCI_SETMARGINWIDTHN, 1, 0);
    Send(SCI_SETMARGINWIDTHN, 2, 0);
}

void Editor::SetFont(std::string_view utf8Face, int points) {
    RequireWindow();
    ValidateUtf8(utf8Face);
    if (utf8Face.empty() || points < 1 || points > 200) {
        throw std::invalid_argument("Footilla font requires a face name and size from 1 to 200 points.");
    }
    const std::string face(utf8Face);
    for (unsigned int style = 0; style <= STYLE_MAX; ++style) {
        Send(SCI_STYLESETFONT, style, reinterpret_cast<LPARAM>(face.c_str()));
        Send(SCI_STYLESETSIZE, style, points);
    }
    SetLineNumbers(lineNumbers_);
}

void Editor::SetExtraFields(std::vector<std::string> names) {
    for (const auto& name : names) {
        ValidateUtf8(name);
    }
    extraFields_ = std::move(names);
}

void Editor::SetExtraFunctions(std::vector<std::string> names) {
    for (const auto& name : names) {
        ValidateUtf8(name);
    }
    extraFunctions_ = std::move(names);
}

void Editor::Focus() {
    RequireWindow();
    SetFocus(scintilla_);
}

void Editor::Restyle() {
    if (!styleDirty_ || !scintilla_) {
        return;
    }
    const auto styles = language::StyleText(GetText());
    styleDirty_ = false;
    Send(SCI_STARTSTYLING, 0);
    if (!styles.empty()) {
        Send(SCI_SETSTYLINGEX, styles.size(), reinterpret_cast<LPARAM>(styles.data()));
    }
}

void Editor::ShowCompletion() {
    RequireWindow();
    if (Send(SCI_GETREADONLY) || Send(SCI_GETSELECTIONEMPTY) == 0) {
        Send(SCI_AUTOCCANCEL);
        return;
    }
    const auto caret = static_cast<std::size_t>(Send(SCI_GETCURRENTPOS));
    const auto completion = language::Complete(GetText(), caret, extraFields_, extraFunctions_);
    if (completion.items.empty()) {
        Send(SCI_AUTOCCANCEL);
        return;
    }
    std::string list;
    for (const auto& item : completion.items) {
        if (!list.empty()) {
            list += '\n';
        }
        list += item;
    }
    Send(SCI_CALLTIPCANCEL);
    Send(SCI_AUTOCSHOW, caret - completion.start, reinterpret_cast<LPARAM>(list.c_str()));
}

void Editor::AcceptCompletion(std::string selected) {
    // SCN_AUTOCSELECTION's text belongs to the popup; copy it before cancelling.
    Send(SCI_AUTOCCANCEL);
    const auto text = GetText();
    const auto caret = static_cast<std::size_t>(Send(SCI_GETCURRENTPOS));
    const auto completion = language::Complete(text, caret, extraFields_, extraFunctions_);
    if (std::find(completion.items.begin(), completion.items.end(), selected) == completion.items.end()) {
        return; // The document/popup context has changed.
    }
    const bool function = selected.front() == '$';
    std::size_t newCaret = completion.start + selected.size();
    if (function) {
        if (completion.end < text.size() && text[completion.end] == '(') {
            ++newCaret;
        } else {
            selected += "()";
            ++newCaret;
            const auto* definition = language::FindFunction(std::string_view(selected).substr(1, selected.size() - 3));
            if (definition && definition->signature.find("()") != std::string_view::npos) {
                ++newCaret;
            }
        }
    }
    editingCompletion_ = true;
    Send(SCI_BEGINUNDOACTION);
    Send(SCI_SETTARGETRANGE, completion.start, completion.end);
    Send(SCI_REPLACETARGET, selected.size(), reinterpret_cast<LPARAM>(selected.c_str()));
    Send(SCI_GOTOPOS, newCaret);
    Send(SCI_ENDUNDOACTION);
    editingCompletion_ = false;
    Restyle();
    UpdateCallTip(true);
}

void Editor::ShowCallTip() {
    RequireWindow();
    UpdateCallTip(true);
}

void Editor::UpdateCallTip(bool force) {
    if (Send(SCI_AUTOCACTIVE) || (!force && !Send(SCI_CALLTIPACTIVE))) {
        return;
    }
    const auto call = language::FindCall(GetText(), static_cast<std::size_t>(Send(SCI_GETCURRENTPOS)));
    const auto* definition = call ? language::FindFunction(call->function) : nullptr;
    if (!definition) {
        Send(SCI_CALLTIPCANCEL);
        return;
    }
    const std::string tip = std::string(definition->signature) + "\n" + std::string(definition->description);
    Send(SCI_CALLTIPSHOW, call->opening, reinterpret_cast<LPARAM>(tip.c_str()));
    const auto signature = definition->signature;
    // Each line is an overload; select one that contains the active argument.
    std::size_t line = 0;
    while (line < signature.size()) {
        auto lineEnd = signature.find('\n', line);
        if (lineEnd == std::string_view::npos) {
            lineEnd = signature.size();
        }
        const auto open = signature.find('(', line);
        const auto close = signature.find(')', line);
        if (open < close && close < lineEnd) {
            auto start = open + 1;
            std::size_t argument = 0;
            while (start < close) {
                const auto comma = signature.find(',', start);
                const auto end = (std::min)(comma, close);
                if (argument == call->argument ||
                    (signature.substr(start, end - start) == "..." && argument < call->argument)) {
                    Send(SCI_CALLTIPSETHLT, start, end);
                    return;
                }
                start = end + 1;
                ++argument;
            }
        }
        line = lineEnd + 1;
    }
}

void Editor::HighlightBraces() {
    const auto caret = Send(SCI_GETCURRENTPOS);
    auto position = caret > 0 ? caret - 1 : caret;
    const auto isBrace = [this](std::intptr_t pos) {
        const auto style = static_cast<language::Style>(Send(SCI_GETSTYLEAT, pos));
        const auto ch = Send(SCI_GETCHARAT, pos);
        return (style == language::Style::Operator || style == language::Style::Conditional) &&
            (ch == '(' || ch == ')' || ch == '[' || ch == ']');
    };
    if (!isBrace(position)) {
        position = caret;
    }
    if (!isBrace(position)) {
        Send(SCI_BRACEHIGHLIGHT, static_cast<std::uintptr_t>(INVALID_POSITION), INVALID_POSITION);
    } else {
        const auto match = Send(SCI_BRACEMATCH, position);
        if (match == INVALID_POSITION) {
            Send(SCI_BRACEBADLIGHT, position);
        } else {
            Send(SCI_BRACEHIGHLIGHT, position, match);
        }
    }
}

void Editor::QueueChange() {
    if (!changePending_) {
        if (!PostMessageW(window_, ChangedMessage, 0, 0)) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Footilla change notification");
        }
        changePending_ = true;
    }
}

void Editor::OnNotify(const SCNotification& notification) {
    switch (notification.nmhdr.code) {
    case SCN_STYLENEEDED:
        Restyle();
        break;
    case SCN_MODIFIED:
        if (notification.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
            styleDirty_ = true;
            QueueChange();
        }
        break;
    case SCN_CHARADDED:
        if (!editingCompletion_) {
            Restyle();
            ShowCompletion();
            if (notification.ch == '(' || notification.ch == ',' || notification.ch == ')') {
                UpdateCallTip(true);
            }
        }
        break;
    case SCN_UPDATEUI:
        if (!editingCompletion_) {
            Restyle();
            HighlightBraces();
            if (Send(SCI_AUTOCACTIVE)) {
                ShowCompletion();
            }
            UpdateCallTip(false);
        }
        break;
    case SCN_ZOOM:
        SetLineNumbers(lineNumbers_);
        break;
    case SCN_AUTOCSELECTION:
        if (notification.text) {
            AcceptCompletion(notification.text);
        }
        break;
    }
}

LRESULT CALLBACK Editor::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        editor = static_cast<Editor*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        editor->window_ = window;
        ++liveEditors;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    }
    if (!editor) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        editor->window_ = nullptr;
        editor->scintilla_ = nullptr;
        editor->changePending_ = false;
        --liveEditors;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    // Never unwind C++ exceptions through a Win32 callback.
    try {
        return editor->OnMessage(message, wParam, lParam);
    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "Footilla error", MB_OK | MB_ICONERROR);
        return message == WM_CREATE ? -1 : 0;
    }
}

LRESULT Editor::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        scintilla_ = CreateWindowExW(0, L"Scintilla", L"Title formatting input",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL,
            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(1), instance, nullptr);
        if (!scintilla_ || !SetWindowSubclass(scintilla_, InputProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
            return -1;
        }
        RECT client{};
        GetClientRect(window_, &client);
        MoveWindow(scintilla_, 0, 0, client.right, client.bottom, TRUE);
        return 0;
    }
    case WM_SIZE:
        MoveWindow(scintilla_, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_SETFOCUS:
        SetFocus(scintilla_);
        return 0;
    case WM_ENABLE:
        EnableWindow(scintilla_, static_cast<BOOL>(wParam));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lParam)->hwndFrom == scintilla_) {
            OnNotify(*reinterpret_cast<SCNotification*>(lParam));
            return 0;
        }
        break;
    case ChangedMessage:
        changePending_ = false;
        SendMessageW(GetParent(window_), WM_COMMAND,
            MAKEWPARAM(GetDlgCtrlID(window_), EN_CHANGE), reinterpret_cast<LPARAM>(window_));
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

LRESULT CALLBACK Editor::InputProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR data) {
    auto* editor = reinterpret_cast<Editor*>(data);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, InputProc, subclassId);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (message == WM_GETDLGCODE) {
        const auto* key = reinterpret_cast<MSG*>(lParam);
        if (key && key->wParam == VK_TAB && !editor->Send(SCI_AUTOCACTIVE)) {
            return DefSubclassProc(window, message, wParam, lParam) & ~(DLGC_WANTTAB | DLGC_WANTALLKEYS);
        }
    }
    if (message == WM_KEYDOWN && wParam == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000)) {
        try {
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                editor->ShowCallTip();
            } else {
                editor->ShowCompletion();
            }
        } catch (const std::exception& error) {
            MessageBoxA(window, error.what(), "Footilla error", MB_OK | MB_ICONERROR);
        }
        return 0;
    }
    if (message == WM_CHAR && wParam == ' ' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

}
