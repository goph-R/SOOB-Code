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
- Builds: `e98.bat` (Win98, COMMAND.COM) and `e10.bat` (Win10). The
  COMMAND.COM rules live in `../SOOB-Core/CLAUDE.md` — goto-only flow,
  `mkdir` one level at a time, and `\nul` on every directory `if exist`.
  `e98.bat`'s comment about 8.3 names is probably a misdiagnosis of that
  last rule: a FILE through `..\SOOB-Core\...` resolves fine on the target.
  Batch files are CRLF (`.gitattributes`).
- Bump `CODEEDIT_VERSION` in `codeedit.cpp` on a feature change.
