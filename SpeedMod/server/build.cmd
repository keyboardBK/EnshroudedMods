@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /O2 /MT /LD /I"..\..\third_party\emf\include" runtime.cpp /Fo:build\runtime.obj /Fe:build\speedmod_server.dll /link /IMPLIB:build\speedmod_server.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /WX /Od /MT /I"..\..\third_party\emf\include" tests\tests.cpp /Fo:build\tests.obj /Fe:build\tests.exe
if errorlevel 1 exit /b 1
build\tests.exe
if errorlevel 1 exit /b 1
