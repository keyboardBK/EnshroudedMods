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
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /O2 /MT /LD /I"..\..\third_party\emf\include" bootlab.cpp /Fo:build\bootlab.obj /Fe:build\speedmod_formula.dll /link /IMPLIB:build\speedmod_formula.lib
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /Od /MT /I"..\..\third_party\emf\include" tests.cpp /Fo:build\tests.obj /Fe:build\tests.exe
if not "%errorlevel%"=="0" exit /b 1
build\tests.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /Od /MT /I"..\..\third_party\emf\include" abi_smoke.cpp /Fo:build\abi_smoke.obj /Fe:build\abi_smoke.exe
if not "%errorlevel%"=="0" exit /b 1
build\abi_smoke.exe build\speedmod_formula.dll
if not "%errorlevel%"=="0" exit /b 1
copy /y mod.json build\mod.json >nul
