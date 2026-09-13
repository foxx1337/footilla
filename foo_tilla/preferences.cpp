#include <SDK/foobar2000.h>

#include <helpers/foobar2000+atl.h>
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>

#include <atlctrlx.h>

#include <Uxtheme.h>

#include "resource.h"

class Preferences : public CDialogImpl<Preferences>, public preferences_page_instance
{
public:
    Preferences(preferences_page_callback::ptr callback)
    {
    }

    // Dialog resource ID.
    static constexpr int IDD = IDD_PREFERENCES;

    t_uint32 get_state() override
    {
        return 0;
    }

    void apply() override
    {
    }

    void reset() override
    {
    }

    // WTL message map
    BEGIN_MSG_MAP_EX(Preferences)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_DESTROY(OnDestroyDialog)
    END_MSG_MAP()

private:
    // Dark mode hooks object, must be a member of dialog class.
    fb2k::CDarkModeHooks dark_mode_;

    // WTL handlers.
    BOOL OnInitDialog(CWindow, LPARAM lParam);
    void OnDestroyDialog();
};

BOOL Preferences::OnInitDialog(CWindow, LPARAM lParam)
{
    // Enable dark mode.
    // One call does it all, applies all relevant hacks automatically.
    dark_mode_.AddDialogWithControls(*this);

    // Don't set keyboard focus to the dialog.
    return FALSE;
}

void Preferences::OnDestroyDialog()
{
    
}

// preferences_page_impl<> helper deals with instantiation of our dialog; inherits from preferences_page_v3.
class preferences_page_footillaimpl : public preferences_page_impl<Preferences>
{
public:
    const char* get_name() override { return "Footilla Playground"; }

    // {328642EF-B0A6-4397-8EFC-176FECF03DB4}
    GUID get_guid() override
    {
        static constexpr GUID guid
        {0x328642ef, 0xb0a6, 0x4397, { 0x8e, 0xfc, 0x17, 0x6f, 0xec, 0xf0, 0x3d, 0xb4}};

        return guid;
    }

    GUID get_parent_guid() override { return guid_tools; }
};

static preferences_page_factory_t<preferences_page_footillaimpl> g_preferences_page_footilla_factory;
