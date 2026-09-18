#include <helpers/foobar2000+atl.h>

#include <libPPUI/win32_op.h>
#include <helpers/BumpableElem.h>

#include <footilla/editor.h>

namespace
{
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

    bool runtime_initialized = false;

    class FootilaPlayground : public ui_element_instance, public CWindowImpl<FootilaPlayground>, public message_filter_impl_base
    {
    public:
        DECLARE_WND_CLASS_EX(L"foo_tilla.FootilaPlayground", CS_HREDRAW | CS_VREDRAW, COLOR_WINDOW)

        FootilaPlayground(ui_element_config::ptr config, ui_element_instance_callback_ptr p_callback)
            : message_filter_impl_base(WM_KEYFIRST, WM_KEYLAST), m_config(config), m_callback(p_callback)
        {}

        HWND get_wnd() override
        {
            return *this;
        }

        void set_configuration(ui_element_config::ptr config) override
        {
            m_config = config;
        }

        ui_element_config::ptr get_configuration() override
        {
            return m_config;
        }

        bool pretranslate_message(MSG* message) override
        {
            if (message->message < WM_KEYFIRST || message->message > WM_KEYLAST ||
                (message->hwnd != m_hWnd && !IsChild(message->hwnd)))
            {
                return false;
            }

            if (IsDialogMessage(message))
            {
                return true;
            }

            TranslateMessage(message);
            DispatchMessage(message);
            return true;
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

        // For atl-misc.h
        void initialize_window(HWND parent) { WIN32_OP(Create(parent) != NULL); }
        // {390EFBE0-51CD-48C9-8439-F7770DF62530}
        static GUID g_get_guid()
        {
            static constexpr GUID guid
            {0x390efbe0, 0x51cd, 0x48c9, { 0x84, 0x39, 0xf7, 0x77, 0xd, 0xf6, 0x25, 0x30}};

            return guid;
        }
        static GUID g_get_subclass() { return ui_element_subclass_utility; }
        static void g_get_name(pfc::string_base& out) { out = "Footilla Playground"; }
        static ui_element_config::ptr g_get_default_configuration() { return ui_element_config::g_create_empty(g_get_guid()); }
        static const char* g_get_description() { return "Footilla playground"; }

    private:
        footilla::Editor editor_;
        CStatic heading_, hint_, legend_, status_;
        CComboBox examples_;
        CButton complete_, callTip_, dark_, wrap_, readOnly_;
        HFONT font_ = nullptr;
        UINT dpi_ = 96;

        int Scale(int size) const { return MulDiv(size, static_cast<int>(dpi_), 96); }

        void UpdateFont()
        {
            const HFONT old = font_;
            font_ = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Segoe UI");
            if (!font_) {
                throw std::runtime_error("Could not create the demo UI font.");
            }
            const HWND children[] = { heading_, hint_, legend_, status_, examples_,
                complete_, callTip_, dark_, wrap_, readOnly_ };
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

                if (!runtime_initialized)
                {
                    if (!footilla::Initialize(core_api::get_my_instance()))
                    {
                        console::printf("foo_tilla: Failed to initialize Footilla. Assuming initialized.");
                    }
                    runtime_initialized = true;
                }

                status_.Create(m_hWnd, empty, L"", WS_CHILD | WS_VISIBLE);
                if (!heading_ || !hint_ || !examples_ || !complete_ || !callTip_ ||
                    !dark_ || !wrap_ || !readOnly_ || !legend_ || !status_ ||
                    !editor_.Create(m_hWnd, EditorId, empty)) {
                    throw std::runtime_error("Could not create the Footilla demo controls.");
                }
                editor_.SetExtraFields({ "datetime" });
                editor_.SetText(Examples[0]);
                UpdateFont();
                Layout();
                UpdateStatus();
                return 0;
            }
            catch (const std::exception& error) {
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
            }
            catch (const std::exception& error) {
                MessageBoxA(m_hWnd, error.what(), "Footilla demo", MB_OK | MB_ICONERROR);
            }
            const auto* bounds = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(nullptr, bounds, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        LRESULT OnMinMax(UINT, WPARAM, LPARAM lParam, BOOL&) {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize = { Scale(830), Scale(440) };
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
            }
            catch (const std::exception& error) {
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
            //PostQuitMessage(0);
            return 0;
        }

        ui_element_config::ptr m_config;
    
    protected:
        // this must be declared as protected for ui_element_impl_withpopup<> to work.
        const ui_element_instance_callback_ptr m_callback;
    };

    // ui_element_impl_withpopup autogenerates standalone version of our component and proper menu commands. Use ui_element_impl instead if you don't want that.
    class ui_element_playgroundimpl : public ui_element_impl_withpopup<FootilaPlayground> {};

    static service_factory_single_t<ui_element_playgroundimpl> g_ui_element_playgroundimpl_factory;
}
