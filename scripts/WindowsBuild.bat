@echo off
rem Host-side one-shot build: MSVC env -> conan install (driven by conan_provider at configure time) -> build -> test.
rem Machine-specific values live only in this script; the CMake project reads QTDIR from the environment.
rem Overridable per machine: set QTDIR / VCVARS before calling.
rem Keep this file ASCII-only: cmd.exe decodes batch files in the console codepage, so Chinese here garbles output.
setlocal

if not defined QTDIR set "QTDIR=G:\Tools\Qt\6.11.1\msvc2022_64"
if not defined VCVARS set "VCVARS=G:\Tools\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=G:\Tools\CMake\bin;G:\Tools\ninja;G:\Tools\Python\Python314\Scripts;%PATH%"
rem Pinned Conan host profile: compiler/runtime/arch/generator come from the file instead of per-machine
rem autodetection. Only compiler.version inside that file is machine-specific.
if not defined CONAN_HOST_PROFILE set "CONAN_HOST_PROFILE=%~dp0..\Conan\Profiles\WindowsMsvc2022"

if "%~1"=="" (set "PRESET=debug") else (set "PRESET=%~1")
cd /d "%~dp0.."

call "%VCVARS%" >nul || exit /b 1
cmake --preset %PRESET% -DCONAN_HOST_PROFILE="%CONAN_HOST_PROFILE%" || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
rem Once a ctest list exists it becomes the gate; with no cases yet the script must not report a false red.
rem The encoder end-to-end cases need a real ffmpeg. Point them at the copy that ships inside the
rem package (vendor/Ffmpeg) when it is there, so the gate exercises the binary users actually get
rem instead of whatever happens to be on this machine's PATH. An operator-set value always wins.
rem Forward slashes on purpose: cmd accepts them, and the file name is then safe to write literally.
if not defined GYANISSCRIBE_FFMPEG if exist "%CD%/vendor/Ffmpeg/ffmpeg.exe" set "GYANISSCRIBE_FFMPEG=%CD%/vendor/Ffmpeg/ffmpeg.exe"
if exist "build\%PRESET%\CTestTestfile.cmake" ctest --preset %PRESET% || exit /b 1
if not exist "build\%PRESET%\CTestTestfile.cmake" echo [skip] no ctest list in this build tree yet
echo [ok] preset %PRESET% built
