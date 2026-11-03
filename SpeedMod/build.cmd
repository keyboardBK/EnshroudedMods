@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VCINSTALLDIR (
  if not exist "%VSWHERE%" exit /b 1
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do call "%%i" >nul
)
if not defined VCINSTALLDIR exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
copy /y runtime\config.ini build\config.ini >nul
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /O2 /MT /LD /I"..\third_party\emf\include" runtime\boots_runtime.cpp /Fo:build\boots_runtime.obj /Fe:build\speedmod_boots.dll /link /IMPLIB:build\speedmod_boots.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /Od /MT /I"..\third_party\emf\include" tests\runtime_tests.cpp /Fo:build\runtime_tests.obj /Fe:build\runtime_tests.exe
if errorlevel 1 exit /b 1
build\runtime_tests.exe
if errorlevel 1 exit /b 1
