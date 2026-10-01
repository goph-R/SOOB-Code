# SOOB-Code — agent notes

The SOOB code editor application. It must build on **Windows 98 with Dev-C++
(GCC 3.4)** as well as Win10 (WinLibs MinGW): no C++11, header-only modules
with `static` functions, C-style casts, `malloc` / `free`. Read
`../SOOB-Core/CLAUDE.md` for the shared conventions.

- Reusable parts live in SOOB-Core, not here: the `CodeEditor` widget
  (`fltk_ui/edit_code.h`), the lexers (`fltk_ui/edit_lex.h`, tests in
  `fltk_ui/edit_code_test.cpp`), `CodeTabs`, DPI / menu helpers, the file
  dialog and encoding-aware file I/O, and the patched FLTK
  (`vendor/fltk-1.3`). Include them as `"fltk_ui/<name>.h"`.
- This repo holds the app: `codeedit.cpp` (menus, documents, status bar,
  settings dialog, recent files) and `edit_settings.h` (`codeedit.ini`).
- Builds: `e98.bat` (Win98, COMMAND.COM) and `e10.bat` (Win10). Win98 batch
  files must not `if exist` a path through `..\SOOB-Core` (not an 8.3 name);
  passing it to the compiler is fine. Batch files are CRLF (`.gitattributes`).
- Bump `CODEEDIT_VERSION` in `codeedit.cpp` on a feature change.
