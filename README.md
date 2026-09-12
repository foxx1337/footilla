# Footilla

A reusable Win32 text input control for foobar2000 title-format expressions,
built as **footilla.lib**. Ships a Footilla-maintained copy of the Scintilla
editing engine used by SciTE, with Footilla's own title-format container lexer,
completion catalog and call tips.
The engine's object files are included in the library: no Scintilla DLL,
Lexilla DLL, SciTE executable, WTL runtime or foobar2000 SDK is required.

The standalone playground uses C++ and WTL. `foo_nowplaying2` is not modified.

## Build

Requires CMake 3.24+, Visual Studio with Desktop development with C++, a Windows
SDK, and ATL for the demo. The demo also requires WTL 10 headers.
The DPI-aware playground targets Windows 10 or later.
Scintilla is included in this repository under `Scintilla`; no external
SciTE/Scintilla checkout or source-path parameter is required.

From this project's directory, using PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DFOOTILLA_WTL_DIR="C:\src\c\WTL10"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\footilla_demo.exe
```

Use the Visual Studio generator installed on your machine. `-A Win32` builds
x86 instead; use a separate build folder. Library, engine and consumer must use
the same architecture and MSVC runtime. The project respects
`CMAKE_MSVC_RUNTIME_LIBRARY` instead of overriding a parent's runtime policy.

The build always uses the repository's maintained Scintilla sources. It does
not download sources, apply patches, or access a sibling SciTE installation.

For the library alone, set `FOOTILLA_BUILD_DEMO=OFF` and
`FOOTILLA_BUILD_TESTS=OFF`; WTL and ATL are then unnecessary.

## Playground

Type `$` for functions or `%` for fields. Suggestions filter as you type,
case-insensitively, including field names containing spaces. Accept with Tab or
Enter; dismiss with Escape. **Ctrl+Space** opens completion at the current token.
Function completion inserts parentheses and positions the caret inside them
(after them for zero-argument functions), without duplicating existing
parentheses. Completing inside a field replaces its suffix and closing `%`.

**Ctrl+Shift+Space** shows a function signature and description. Typing `(` or
`,` opens argument help; nested calls and quoted commas are handled. The
current parameter is highlighted. Standard Scintilla selection, clipboard,
undo/redo, Unicode editing and brace matching remain available.

Select an example or toggle the editor's dark palette, wrapping and read-only
mode. Outside a popup, Tab/Shift+Tab traverse the demo's controls.
`%datetime%` demonstrates a host-supplied field, not a foobar2000 built-in.

### Visual indentation

Footilla displays a stepped left inset based on nested function calls and
conditional sections. Each level is two space-width columns by default.
The line-number gutter stays straight; the blank strip between it and the text
uses the gutter background. Leading closing delimiters align with their openers,
and blank lines retain the enclosing level.

**The inset is not whitespace.** Enter inserts only the normal newline; it does
not insert spaces or tabs. Text access, selection, copy/paste, save, and undo
continue to operate on the original document bytes. Home goes to the first real
character, and clicking the blank inset positions the caret there. Literal
spaces already present in the expression are neither removed nor hidden.
Comments, quoted literals and field names do not introduce nesting.

Set `Options::visualIndentationWidth` before `Create()`, or call
`Editor::SetVisualIndentationWidth(columns)` on a live control. The getter is
`GetVisualIndentationWidth()`. Zero disables visual indentation; negative widths
are rejected. Changing this display setting does not modify text or undo history.
The width follows the editor font, zoom and DPI. Wrapped continuation lines keep
their document line's inset; extreme nesting is visually limited to leave room
for text when wrapping. This is automatic syntax layout, not manual Tab-based
indentation.

Highlighting distinguishes fields, functions, quoted text, line comments,
conditional sections, punctuation, numbers and unfinished lexical constructs.
This is **editing assistance, not a title-format evaluator or full validator**.
Unknown fields/functions remain valid-looking because components can define
their own. Availability of suggested playback/playlist/component fields depends
on the host's evaluation context. Full-document styling is intentional for
short title-format expressions, rather than large source files.

## Embed in a CMake application or plugin

```cmake
set(FOOTILLA_BUILD_DEMO OFF CACHE BOOL "")
set(FOOTILLA_BUILD_TESTS OFF CACHE BOOL "")
add_subdirectory(path/to/footilla)
target_link_libraries(your_target PRIVATE footilla::footilla)
```

The target propagates C++17, public headers and required Windows libraries.
The public API does not depend on WTL or the foobar2000 SDK.

```cpp
#include <footilla/editor.h>

// Once on your UI thread, outside DllMain. Check the return value.
if (!footilla::Initialize(moduleInstance)) {
    // Report GetLastError() to the host.
    return false;
}

// Keep this object alive for the lifetime of the control (e.g. a dialog member).
footilla::Editor editor;
RECT bounds{10, 10, 600, 240};
if (!editor.Create(parentWindow, 1001, bounds)) {
    return false;
}
editor.SetExtraFields({"datetime"});
editor.SetText("$if2(%artist%,Unknown artist) - %title%");
std::string utf8 = editor.GetText();
```

`SetText`/`GetText` use UTF-8; positions exposed by the language core and
Scintilla use byte offsets, not UTF-16 positions. `SetText` rejects malformed
UTF-8 and embedded NUL with `std::invalid_argument`, clears undo history, and can
update a read-only editor. Other editing operations respect read-only mode.

Move/enable the host HWND returned by `Handle()` as a normal child window.
Use `Focus()` to enter it. Parent dialogs receive coalesced
`WM_COMMAND / EN_CHANGE`, with the host control ID and `Handle()` in `lParam`;
read updated text using `GetText()`. No `WM_NOTIFY` forwarding is required.
`ScintillaHandle()` is available for advanced Scintilla messages; do not replace
its document, lexer, line-inset metadata or notification settings behind Footilla.

This is not a binary-compatible replacement for the Windows `EDIT` class:
use the API instead of `WM_GETTEXT`, `WM_SETTEXT`, or `EM_*` messages.
All editor calls and object lifetimes belong to the initializing UI thread.
Destroying the parent detaches the C++ object; destroying the object destroys
its window. Destroy all editors before calling `footilla::Shutdown()`, outside
`DllMain`, before unloading a plugin. `Initialize` is idempotent, not
reference-counted. `Shutdown` rejects live editors.

Scintilla registers process-global window classes. Do not initialize a second
independently linked Scintilla instance in the same process; initialization
fails rather than silently attaching to another component's engine. A host
already embedding Scintilla needs a coordinated shared engine integration.
The demo initializes OLE on its UI thread for Scintilla drag/drop; embedders
should use their host's existing OLE initialization.

## Source and redistribution

Syntax and the built-in catalog are based on the
[Hydrogenaudio title-format reference](https://wiki.hydrogenaudio.org/index.php?title=Foobar2000:Title_Formatting_Reference).
Catalog descriptions are concise original summaries, not copied article text.

### Maintained Scintilla sources

`Scintilla` contains the static Win32 engine sources and their transitive header
dependencies, copied unchanged from **Scintilla 5.6.6** on 2026-09-12. The original
SciTE/Scintilla source folder is not modified. `Scintilla\version.txt` records
the upstream baseline (`566`); `Scintilla\License.txt` retains the upstream
copyright and redistribution terms.

`Scintilla\src` contains the editor core, `Scintilla\win32` the Windows backend,
and `Scintilla\include` the required public headers and `Scintilla.iface`
interface definition. The initial source list follows the static
`COMPONENT_OBJS` from upstream `win32\scintilla.mak`. DLL entry points/resources,
other platform backends, SciTE, Lexilla and upstream build/generation tools are
not included.

These are ordinary, Footilla-maintained source files, not a submodule or a
build-time overlay. Future engine changes belong directly in this tree;
upstream updates are deliberate merges with the copyright notices preserved.
If an interface change requires regenerating headers, use the matching upstream
generation tools and include the generated headers in the change.
`cmake\Scintilla.cmake` compiles this tree as the `footilla_scintilla` object
target; its objects are included in `footilla.lib`.

The maintained engine adds generic, view-local line insets. The private
`Scintilla\include\ScintillaFootilla.h` messages set a complete array of
nonnegative inset columns and query a line's configured inset. These messages
are Footilla extensions, not upstream Scintilla APIs. `EditModel` owns the
metadata, `Editor` invalidates affected layout, and `EditView` applies the same
pixel inset to painting, caret coordinates, mouse/rectangular hit-testing,
wrapping and horizontal extent tracking. The renderer also uses the inset when
printing. No synthetic characters or runtime hooks are used. Each Scintilla view
defaults to zero inset until its host supplies metadata.

Footilla's language scanner computes the levels and republishes them after
insertions/deletions, including paste and undo/redo. The engine contains no
foobar2000 grammar. The initial upstream baseline remains recorded as 5.6.6;
the code in this tree now includes Footilla's maintained changes.

Redistributing `footilla.lib` or an application linked to it also redistributes
Scintilla. Include the upstream notice copied by CMake to
`build\Scintilla-License.txt` and next to `footilla.lib`. WTL is used only by the
playground; its source distribution's `MS-PL.txt` is copied next to the demo as
`WTL-License.txt` when available.
