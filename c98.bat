@echo off
echo === CodeEditor probe build ===
REM  Minimal CodeEditor widget on its own (codetest.cpp). Same layout as
REM  e98.bat: run from the SOOB-Code root, FLTK built in ..\SOOB-Core.
if not exist codeedit.cpp goto nocwd
set ENGINE=..\SOOB-Core
set FLTK=%ENGINE%\vendor\fltk-1.3\FL
set DC=C:\Dev-Cpp\bin
REM ---- No "if exist" on a directory: the usual dir\nul test is a local
REM ---- DOS quirk and misfires over the share this tree is built from,
REM ---- printing "File not found". Just make them and bin the complaint.
md raw     >nul
md raw\obj >nul
%DC%\g++.exe -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I. -I%ENGINE% -I%FLTK% -O2 -c codetest.cpp -o raw\obj\c98.o
if errorlevel 1 goto error
%DC%\g++.exe raw\obj\c98.o -o c98.exe -L%FLTK%\lib -lfltk -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwsock32
if errorlevel 1 goto error
echo.
echo === Built c98.exe - run:  c98            (built-in demo) ===
echo ===                       c98 file.lua   (open a file)   ===
goto end
:nocwd
echo ERROR: run this from the SOOB-Code root.
goto error
:error
echo.
echo === PROBE BUILD FAILED ===
pause
:end
