# SOOB-Code

A small, fast code editor in the spirit of the mid-2000s Notepad++ — built
with a patched FLTK 1.3 so that the same source runs on **Windows 98** (Dev-C++
/ GCC 3.4) and on Windows 10 / 11 (WinLibs MinGW).

![SOOB Code Editor 1.0 on Windows 98](docs/screenshot-v1.png)

*Version 1.0 on Windows 98: Markdown highlighting, tabs, Find / Replace and
the status bar.*

## Features

- Tabs, Find / Replace (Enter = Find Next), Go to line, undo / redo
- File / Reload (Ctrl+R) re-reads the file from disk, asking first if it
  has unsaved changes
- Syntax highlighting: Bash / sh, Batch (`.bat` / `.cmd`), C / C++ (also
  `.glsl` / `.vert` / `.frag` / `.inl` / `.rc`), CSS, HTML (with `<script>` /
  `<style>`), INI (`.ini` / `.cfg` / `.inf`), Java, JavaScript (also `.ts` /
  `.tsx`), JSON (keys told apart from string values), Lua, Markdown (a
  fenced block colours as its language -- ```` ```php ````, ```` ```bash ````
  and every other tag on this list; `~~~` fences stay flat), Pascal,
  PHP (HTML templates with `<?php ?>` blocks), Python, SQL,
  XML (`.xml` / `.xsd` / `.xsl` / `.svg` / `.tmx` / `.qrc` / `.csproj`)
- Auto-indent, Tab / Shift+Tab block indent; `}` `]` `)` re-indent to their
  opener, and so do Lua's `end` / `until` / `else` and Pascal's
  `end` / `until` / `except` / `finally`
- Trailing whitespace stripped on save (optional, on by default; Markdown
  keeps a two-space hard line break)
- Smart Home, centre-on-find, re-indent on paste
- Right-click menu (Cut / Copy / Paste / Delete), and on a tab: Copy
  absolute path, or Select in Folders to find that file in the sidebar
  (it comes out if hidden; nothing is added to it); Tools / Format JSON
  (Alt+J) pretty-prints the selection or the whole file with the Tab
  setting, or puts the caret on the first error
- Matching pairs: the bracket at the caret and its partner ( ) [ ] { } are
  outlined (red if unmatched), and in HTML / PHP both names of an element
- The word at the caret is underlined wherever else it is on screen, so a
  name can be followed by eye while scrolling. Whole words, exact spelling,
  no parsing: keywords, types and numbers are left alone, everything else
  is a name. Double-click one to the same effect. View / Highlight Word at
  Caret turns it off, as does the Settings checkbox -- both remembered
- Encodings: UTF-8, UTF-8 with BOM, ANSI (system code page) — detected on
  open, kept on save; CRLF / LF kept too
- Folders sidebar (View / Folders): add folders with File / Open Folder or
  the tree's right-click menu; double-click opens a file in a tab, or hands
  it to Windows if the editor cannot read it (those are drawn dimmed).
  Hidden and system files are skipped; drag the edge to resize. Refresh on
  the right-click menu re-reads a folder that changed on disk, and Open as
  Text opens a file the editor would otherwise refuse -- an `.obj`, a `.dat`,
  anything with an extension it has not been taught. A big one asks first, and
  a file with a zero byte in it is only loaded as far as that byte, so Save
  asks for another name rather than truncate it
- Find in Folder (Ctrl+Shift+F) searches the current project and everything
  under it -- the sidebar folder holding the active tab's file, or the one
  holding the tree selection, or the first listed. Double-click a result to open that file on the line;
  Replace All asks once, then edits open tabs through their undo and the
  rest on disk, keeping each file's encoding and line ending
- Status bar (Ln/Col, language, line ending, encoding), recent files,
  word wrap, settings (`codeedit.ini` next to the exe)
- Single instance: opening more files (e.g. F4 in Total Commander) adds tabs
  to the running window instead of starting another one
- DPI-aware on Vista+ (Segoe UI / Consolas), plain Arial / Courier New on Win98

## Layout

SOOB-Code is the application. The editor widget, the lexers, the tabs and the
patched FLTK live in **[SOOB-Core](https://github.com/goph-R/SOOB-Core)**, which must sit beside this folder:

```
Projects\
  SOOB-Core\     vendor\fltk-1.3 (patched FLTK), fltk_ui\ (CodeEditor & co.)
  SOOB-Code\     this repo: codeedit.cpp, edit_settings.h, build scripts
```

## Building

1. Build FLTK once, in SOOB-Core: `fltk98.bat` (Win98), `build_fltk.bat`
   (Dev-C++ under cmd.exe) or `build_fltk_win10.bat` (Win10).
2. Here: `e98.bat` → `codeedit.exe`, or `e10.bat` → `codeedit_w10.exe`.

Probes: `c98.bat` (the CodeEditor widget alone) and `f98.bat` (the Win98 file
dialog / directory listing).

See [`SOOB-Core/docs/editor-fltk-win98.md`](https://github.com/goph-R/SOOB-Core/blob/main/docs/editor-fltk-win98.md) for the FLTK recipe and every local
FLTK patch.
