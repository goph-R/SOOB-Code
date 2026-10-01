@echo off
setlocal

echo === SOOB Code Editor - Win10 MinGW Build ===
echo.

REM ----------------------------------------------------------------
REM  Win10 twin of e98.bat: the portable WinLibs i686 MinGW under
REM  ..\SOOB-Core\vendor_win10\ instead of C:\Dev-Cpp.
REM
REM  Run from the SOOB-Code root. Prerequisite: build_fltk_win10.bat in
REM  SOOB-Core -- it produces the static libs in %FLTK%\lib_w10 (separate
REM  from the Dev-C++ lib\, the two toolchains' archives are not ABI
REM  compatible).
REM ----------------------------------------------------------------

set "ENGINE=..\SOOB-Core"
set "GPP=%ENGINE%\vendor_win10\mingw32\bin\g++.exe"
set "FLTK=%ENGINE%\vendor\fltk-1.3\FL"
set "OBJDIR=raw\obj"

if not exist codeedit.cpp (
    echo ERROR: run this from the SOOB-Code root.
    goto error
)
if not exist "%GPP%" (
    echo ERROR: MinGW not found at %GPP%
    echo Download WinLibs i686 and extract to %ENGINE%\vendor_win10\mingw32\
    echo https://github.com/brechtsanders/winlibs_mingw/releases
    goto error
)
if not exist "%FLTK%\lib_w10\libfltk.a" (
    echo ERROR: FLTK libraries not built. Run build_fltk_win10.bat in SOOB-Core first.
    goto error
)

if not exist "%OBJDIR%" mkdir "%OBJDIR%"

echo Compiling code editor...
%GPP% -DWIN32 -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -I. -I%ENGINE% -I%FLTK% -O2 -c codeedit.cpp -o %OBJDIR%\e10.o
if errorlevel 1 goto error

REM ----------------------------------------------------------------
REM  Link: FLTK core + Win32. -static-libgcc/-static-libstdc++ keeps
REM  the exe free of MinGW runtime DLLs.
REM ----------------------------------------------------------------
echo Linking...
%GPP% %OBJDIR%\e10.o -o codeedit_w10.exe -L%FLTK%\lib_w10 -lfltk -lole32 -luuid -lcomctl32 -lcomdlg32 -lgdi32 -lwinspool -lwsock32 -static-libgcc -static-libstdc++
if errorlevel 1 goto error

echo.
echo === Built codeedit_w10.exe - run:  codeedit_w10 [file ...] ===
goto end

:error
echo.
echo === BUILD FAILED ===
pause

:end
endlocal
