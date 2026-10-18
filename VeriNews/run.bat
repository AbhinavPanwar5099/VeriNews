@echo off
REM VeriNews Lite - double-click to compile and run.
cd /d "%~dp0"
where g++ >nul 2>nul && goto compile
for %%D in ("C:\Program Files\CodeBlocks\MinGW\bin" "C:\Program Files (x86)\CodeBlocks\MinGW\bin" "C:\Program Files (x86)\Embarcadero\Dev-Cpp\TDM-GCC-64\bin" "C:\Program Files (x86)\Dev-Cpp\MinGW64\bin" "C:\msys64\ucrt64\bin" "C:\msys64\mingw64\bin" "C:\MinGW\bin") do if exist "%%~D\g++.exe" set "GPPDIR=%%~D"
if not defined GPPDIR goto nocompiler
set "PATH=%GPPDIR%;%PATH%"
:compile
echo Compiling...
g++ -std=c++11 main.cpp -o verinews.exe
if errorlevel 1 goto failed
verinews.exe
pause
exit /b 0
:nocompiler
echo No C++ compiler found. Install Code::Blocks (the "mingw-setup.exe" version).
pause
exit /b 1
:failed
echo Compilation failed. Copy the messages above and ask for help.
pause
exit /b 1
