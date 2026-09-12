#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct SCNotification;

namespace footilla {

// Call on the UI thread, outside DllMain. Pass the module linking footilla.lib.
// Failures return false and set GetLastError(). Shutdown requires no live editors.
bool Initialize(HINSTANCE module);
bool Shutdown();

enum class Theme { Light, Dark };

struct Options {
    Theme theme = Theme::Light;
    bool lineNumbers = true;
    bool wordWrap = false;
    bool readOnly = false;
    int fontSizePoints = 11;
    int visualIndentationWidth = 2;
};

// Parent destruction detaches the object; object destruction destroys the window.
// All methods and the object's lifetime belong to the Initialize() UI thread.
class Editor final {
public:
    Editor() = default;
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) = delete;
    Editor& operator=(Editor&&) = delete;

    bool Create(HWND parent, int controlId, const RECT& bounds, const Options& options = {});
    void Destroy();
    HWND Handle() const noexcept { return window_; }
    HWND ScintillaHandle() const noexcept { return scintilla_; }

    // UTF-8, no embedded NUL. SetText throws std::invalid_argument for invalid UTF-8.
    // Programmatic SetText works in read-only mode and starts a fresh undo history.
    void SetText(std::string_view text);
    std::string GetText() const;
    void SetReadOnly(bool readOnly);
    void SetTheme(Theme theme);
    void SetWordWrap(bool enabled);
    void SetLineNumbers(bool enabled);
    void SetFont(std::string_view utf8Face, int points);
    // Display-only columns per syntax level. Zero disables; never inserts whitespace.
    void SetVisualIndentationWidth(int columns);
    int GetVisualIndentationWidth() const noexcept { return visualIndentationWidth_; }
    void SetExtraFields(std::vector<std::string> names);
    void SetExtraFunctions(std::vector<std::string> names);
    void ShowCompletion();
    void ShowCallTip();
    void Focus();

    // Parent receives coalesced WM_COMMAND / EN_CHANGE, with Handle() in lParam.
    // HWND-only operations must not outlive the object.
private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK InputProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT OnMessage(UINT, WPARAM, LPARAM);
    void OnNotify(const SCNotification&);
    std::intptr_t Send(unsigned int message, std::uintptr_t wParam = 0, std::intptr_t lParam = 0) const;
    void RequireWindow() const;
    void Restyle();
    void UpdateVisualInsets();
    void UpdateCallTip(bool force);
    void AcceptCompletion(std::string selected);
    void HighlightBraces();
    void QueueChange();

    HWND window_ = nullptr;
    HWND scintilla_ = nullptr;
    bool styleDirty_ = true;
    bool changePending_ = false;
    bool editingCompletion_ = false;
    bool lineNumbers_ = true;
    int visualIndentationWidth_ = 2;
    std::vector<std::string> extraFields_;
    std::vector<std::string> extraFunctions_;
};

}
