// Footilla extensions to the maintained Scintilla engine (not upstream messages).
#ifndef SCINTILLA_FOOTILLA_H
#define SCINTILLA_FOOTILLA_H

namespace Scintilla::Footilla {

// wParam: document line count; lParam: pointer to that many nonnegative int columns.
// Copied synchronously into this view; 0/null clears all insets.
// Returns 1 on success, 0 with SCI_GETSTATUS == SC_STATUS_FAILURE on invalid input.
// Does not change text, selections, the modified flag, or undo history.
constexpr unsigned int SetLineInsets = 0x6000;

// wParam: document line; returns its configured inset in space-width columns.
constexpr unsigned int GetLineInset = 0x6001;

}
#endif
