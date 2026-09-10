#include <footilla/editor.h>
#include <footilla/language.h>
#include <Scintilla.h>
#include <ole2.h>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int changes = 0;
HWND changedWindow = nullptr;

void Check(bool condition, const char* description) {
    if (!condition) {
        throw std::runtime_error(description);
    }
}

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == EN_CHANGE) {
        ++changes;
        changedWindow = reinterpret_cast<HWND>(lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Complete(footilla::Editor& editor, const char* item) {
    editor.ShowCompletion();
    Check(SendMessageW(editor.ScintillaHandle(), SCI_AUTOCACTIVE, 0, 0) != 0, "Completion popup not active");
    SendMessageW(editor.ScintillaHandle(), SCI_AUTOCSELECT, 0, reinterpret_cast<LPARAM>(item));
    SendMessageW(editor.ScintillaHandle(), SCI_AUTOCCOMPLETE, 0, 0);
    Pump();
}
}

int main() {
    HWND parent = nullptr;
    bool initialized = false;
    const auto module = GetModuleHandleW(nullptr);
    const auto ole = OleInitialize(nullptr);
    try {
        Check(SUCCEEDED(ole), "OLE initialization");
        Check(footilla::Initialize(module), "Footilla initialization");
        initialized = true;
        Check(footilla::Initialize(module), "Repeated initialization should be idempotent");
        WNDCLASSW wc{};
        wc.lpfnWndProc = ParentProc;
        wc.hInstance = module;
        wc.lpszClassName = L"Footilla.TestHost";
        Check(RegisterClassW(&wc) != 0, "Parent class registration");
        parent = CreateWindowW(wc.lpszClassName, L"Footilla tests", WS_OVERLAPPEDWINDOW,
            0, 0, 700, 400, nullptr, nullptr, module, nullptr);
        Check(parent != nullptr, "Parent creation");
        {
            footilla::Editor editor;
            footilla::Editor second;
            const RECT bounds{0, 0, 640, 320};
            Check(editor.Create(parent, 101, bounds), "Editor creation");
            Check(second.Create(parent, 102, bounds), "Second editor creation");
            Check(!footilla::Shutdown() && GetLastError() == ERROR_BUSY, "Shutdown must reject live editors");
            const std::string text = u8"$if(%artist%,'caf\u00e9',%title%)[ - %album%]\r\n// comment";
            editor.SetText(text);
            Check(editor.GetText() == text, "UTF-8 round trip");
            const auto styles = footilla::language::StyleText(text);
            for (std::size_t i = 0; i < text.size(); ++i) {
                Check(SendMessageW(editor.ScintillaHandle(), SCI_GETSTYLEAT, i, 0) == styles[i],
                    "Control styling differs from language core");
            }
            Pump();
            Check(changes > 0 && changedWindow == editor.Handle(), "EN_CHANGE notification source");
            Check(second.GetText().empty(), "Editor documents must be independent");

            editor.SetReadOnly(true);
            SendMessageW(editor.ScintillaHandle(), SCI_INSERTTEXT, 0, reinterpret_cast<LPARAM>("blocked"));
            Check(editor.GetText() == text, "Read-only typing protection");
            editor.SetText("%alb");
            Check(editor.GetText() == "%alb", "Programmatic read-only update");
            Check(SendMessageW(editor.ScintillaHandle(), SCI_GETREADONLY, 0, 0) != 0,
                "SetText must preserve read-only mode");
            editor.ShowCompletion();
            Check(!SendMessageW(editor.ScintillaHandle(), SCI_AUTOCACTIVE, 0, 0), "Read-only popup suppression");
            editor.SetReadOnly(false);

            editor.SetText("");
            SendMessageW(editor.ScintillaHandle(), WM_CHAR, '$', 0);
            Check(SendMessageW(editor.ScintillaHandle(), SCI_AUTOCACTIVE, 0, 0) != 0,
                "Typing a dollar must automatically open completion");
            SendMessageW(editor.ScintillaHandle(), SCI_AUTOCCANCEL, 0, 0);
            editor.SetText("");
            SendMessageW(editor.ScintillaHandle(), WM_CHAR, '%', 0);
            Check(SendMessageW(editor.ScintillaHandle(), SCI_AUTOCACTIVE, 0, 0) != 0,
                "Typing a percent must automatically open completion");
            SendMessageW(editor.ScintillaHandle(), SCI_AUTOCCANCEL, 0, 0);

            editor.SetText("%album artist%");
            SendMessageW(editor.ScintillaHandle(), SCI_GOTOPOS, 4, 0);
            Complete(editor, "%album%");
            Check(editor.GetText() == "%album%", "Field midpoint completion must replace suffix and closing percent");
            SendMessageW(editor.ScintillaHandle(), SCI_UNDO, 0, 0);
            Check(editor.GetText() == "%album artist%", "Completion must be one undo action");

            editor.SetText("$ifg");
            SendMessageW(editor.ScintillaHandle(), SCI_GOTOPOS, 4, 0);
            Complete(editor, "$ifgreater");
            Check(editor.GetText() == "$ifgreater()", "Function completion parentheses");
            Check(SendMessageW(editor.ScintillaHandle(), SCI_GETCURRENTPOS, 0, 0) == 11,
                "Function caret should be inside parentheses");
            Check(SendMessageW(editor.ScintillaHandle(), SCI_CALLTIPACTIVE, 0, 0) != 0, "Function call tip");

            editor.SetText("$upper(%title%)");
            SendMessageW(editor.ScintillaHandle(), SCI_GOTOPOS, 3, 0);
            Complete(editor, "$upper");
            Check(editor.GetText() == "$upper(%title%)", "Existing function parentheses must not duplicate");

            editor.SetText("$cr");
            SendMessageW(editor.ScintillaHandle(), SCI_GOTOPOS, 3, 0);
            Complete(editor, "$crlf");
            Check(editor.GetText() == "$crlf()", "Zero-argument function insertion");
            Check(SendMessageW(editor.ScintillaHandle(), SCI_GETCURRENTPOS, 0, 0) == 7,
                "Zero-argument function caret should follow parentheses");

            editor.SetExtraFields({"datetime"});
            editor.SetText("%date");
            SendMessageW(editor.ScintillaHandle(), SCI_GOTOPOS, 5, 0);
            Complete(editor, "%datetime%");
            Check(editor.GetText() == "%datetime%", "Host-supplied fields");

            editor.SetText("%ALBUM AR");
            SendMessageW(editor.ScintillaHandle(), SCI_GOTOPOS, 9, 0);
            Complete(editor, "%album artist%");
            Check(editor.GetText() == "%album artist%", "Spaces and case-insensitive completion");
            editor.SetExtraFunctions({"host_function"});
            editor.SetText("$host");
            SendMessageW(editor.ScintillaHandle(), SCI_GOTOPOS, 5, 0);
            Complete(editor, "$host_function");
            Check(editor.GetText() == "$host_function()", "Host-supplied functions");

            editor.SetText("'%alb'");
            SendMessageW(editor.ScintillaHandle(), SCI_GOTOPOS, 5, 0);
            editor.ShowCompletion();
            Check(!SendMessageW(editor.ScintillaHandle(), SCI_AUTOCACTIVE, 0, 0), "No completion in quoted literals");
            editor.SetTheme(footilla::Theme::Dark);
            editor.SetWordWrap(true);
            Check(SendMessageW(editor.ScintillaHandle(), SCI_GETWRAPMODE, 0, 0) == SC_WRAP_WORD, "Wrap toggle");
            editor.SetLineNumbers(false);
            Check(SendMessageW(editor.ScintillaHandle(), SCI_GETMARGINWIDTHN, 0, 0) == 0, "Margin toggle");

            bool rejected = false;
            try {
                editor.SetText(std::string("bad\0text", 8));
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            Check(rejected, "Reject embedded NUL");
            rejected = false;
            try {
                editor.SetText("\xff");
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            Check(rejected, "Reject malformed UTF-8");
            Check(DestroyWindow(parent) != FALSE, "Destroy parent");
            parent = nullptr;
            Check(!editor.Handle() && !second.Handle(), "Parent destruction must detach editors");
            Pump();
            parent = CreateWindowW(wc.lpszClassName, L"Footilla tests", WS_OVERLAPPEDWINDOW,
                0, 0, 700, 400, nullptr, nullptr, module, nullptr);
            Check(parent != nullptr && editor.Create(parent, 101, bounds), "Recreate a detached editor");
            editor.SetText("%title%");
            Check(editor.GetText() == "%title%", "Recreated editor text");
            editor.Destroy();
            Check(!editor.Handle(), "Explicit editor destruction");
            Check(DestroyWindow(parent) != FALSE, "Destroy second parent");
            parent = nullptr;
        }
        Check(footilla::Shutdown(), "Resource shutdown");
        initialized = false;
        Check(footilla::Initialize(module), "Reinitialize after shutdown");
        initialized = true;
        Check(footilla::Shutdown(), "Second shutdown");
        initialized = false;
        UnregisterClassW(L"Footilla.TestHost", module);
        OleUninitialize();
        std::cout << "All native control tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (Windows error " << GetLastError() << ")\n";
        if (parent) {
            DestroyWindow(parent);
        }
        if (initialized) {
            footilla::Shutdown();
        }
        if (SUCCEEDED(ole)) {
            OleUninitialize();
        }
        return 1;
    }
}
