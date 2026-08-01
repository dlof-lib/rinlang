@echo off
REM cli\windows\build.bat
REM يبني rin.exe تلقائياً: يفضّل MSVC (cl.exe) إن كان متاحاً في PATH (شغّل هذا
REM الملف من داخل "Developer Command Prompt for VS")، وإلا يجرّب MinGW (g++).
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [rin] CMake غير موجود في PATH. حمّله من https://cmake.org/download
    exit /b 1
)

where cl >nul 2>nul
if not errorlevel 1 (
    echo [rin] تم العثور على MSVC ^(cl^) - البناء...
    cmake -B build || goto :fail
    cmake --build build --config Release || goto :fail
    echo.
    echo [rin] تم البناء بنجاح. ابحث عن rin.exe داخل build\Release\ ^(أو build\^)
    goto :end
)

where g++ >nul 2>nul
if not errorlevel 1 (
    echo [rin] لم يُعثر على MSVC، البناء باستخدام MinGW ^(g++^)...
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release || goto :fail
    cmake --build build || goto :fail
    echo.
    echo [rin] تم البناء بنجاح: build\rin.exe
    goto :end
)

echo [rin] لم يُعثر على MSVC ^(cl^) ولا MinGW ^(g++^) في PATH.
echo       - MSVC: ثبّت "Visual Studio Build Tools" مع حزمة "Desktop development with C++"
echo                ثم شغّل هذا الملف من داخل "Developer Command Prompt for VS".
echo       - MinGW: https://www.mingw-w64.org  أو  choco install mingw
exit /b 1

:fail
echo.
echo [rin] فشل البناء - راجع الرسائل أعلاه.
exit /b 1

:end
endlocal
