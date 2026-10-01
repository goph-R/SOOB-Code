@echo off
echo === SOOB Code Editor - Win98/Dev-C++ Build ===
echo.

REM ----------------------------------------------------------------
REM  Run from the SOOB-Code root, with SOOB-Core beside it (..\SOOB-Core)
REM  and its FLTK built first (fltk98.bat there):
REM      X:
REM      cd \Projects\SOOB-Code
REM      e98
REM
REM  No "if exist" on the FLTK libs: COMMAND.COM's builtins resolve 8.3
REM  names only and "SOOB-Core" is not one. If FLTK is not built yet,
REM  the link stops with "cannot find -lfltk".
REM ----------------------------------------------------------------

REM ---- Current-directory check. codeedit.cpp is a valid 8.3 name.
if not exist codeedit.cpp goto nocwd

set ENGINE=..\SOOB-Core
set FLTK=%ENGINE%\vendor\fltk-1.3\FL
set DC=C:\Dev-Cpp\bin

if not exist raw\nul mkdir raw
if not exist raw\obj\nul mkdir raw\obj

REM ---- -mwindows on the link: a GUI program, no console window.
REM ---- -I%ENGINE% resolves the shared widgets ("fltk_ui/edit_code.h"),
REM ---- -I%FLTK% resolves <FL/...>.
%DC%\g++.exe -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I. -I%ENGINE% -I%FLTK% -O2 -c codeedit.cpp -o raw\obj\e98.o
if errorlevel 1 goto error
%DC%\g++.exe raw\obj\e98.o -mwindows -o codeedit.exe -L%FLTK%\lib -lfltk -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwsock32
if errorlevel 1 goto error

echo.
echo === Built codeedit.exe - run:  codeedit [file ...] ===
goto end

:nocwd
echo ERROR: run this from the SOOB-Code root.
goto error

:error
echo.
echo === BUILD FAILED ===
pause

:end
