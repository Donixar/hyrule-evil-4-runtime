@echo off
setlocal
rem Compile only; never install or start the game. Requires the local x86 VS toolchain.
if not defined VCVARS32 set "VCVARS32=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
call "%VCVARS32%" >nul
if errorlevel 1 exit /b 1
set "WK=C:\Program Files (x86)\Windows Kits\10"
set "WKVER=10.0.22621.0"
set "INCLUDE=%INCLUDE%;%WK%\include\%WKVER%\um;%WK%\include\%WKVER%\shared"
set "LIB=%LIB%;%WK%\lib\%WKVER%\um\x86"
cd /d "%~dp0.."
set "BUILD=work\link-runtime"
if not "%~1"=="" set "BUILD=%~1"
if not exist "%BUILD%" mkdir "%BUILD%"
if /i "%~2"=="--verify-existing-minhook" (
  python scripts\fetch_minhook.py "%BUILD%" --verify-existing || exit /b 1
) else (
  python scripts\fetch_minhook.py "%BUILD%" || exit /b 1
)
set "MH=%BUILD%\minhook-src"
cl /nologo /W3 /O2 /MT /c "%MH%\src\buffer.c" /Fo"%BUILD%\mh_buffer.obj" || exit /b 1
cl /nologo /W3 /O2 /MT /c "%MH%\src\hook.c" /Fo"%BUILD%\mh_hook.obj" || exit /b 1
cl /nologo /W3 /O2 /MT /c "%MH%\src\trampoline.c" /Fo"%BUILD%\mh_trampoline.obj" || exit /b 1
cl /nologo /W3 /O2 /MT /c "%MH%\src\hde\hde32.c" /Fo"%BUILD%\mh_hde32.obj" || exit /b 1
lib /nologo /OUT:"%BUILD%\minhook.lib" "%BUILD%\mh_buffer.obj" "%BUILD%\mh_hook.obj" "%BUILD%\mh_trampoline.obj" "%BUILD%\mh_hde32.obj" || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_link_runtime_core.cpp /Fo"%BUILD%\test_core.obj" /Fe"%BUILD%\test_core.exe" || exit /b 1
"%BUILD%\test_core.exe" || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_link_runtime_health.cpp /Fo"%BUILD%\test_health.obj" /Fe"%BUILD%\test_health.exe" || exit /b 1
"%BUILD%\test_health.exe" || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_low_health.cpp /Fo"%BUILD%\test_low_health.obj" /Fe"%BUILD%\test_low_health.exe" || exit /b 1
"%BUILD%\test_low_health.exe" || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_link_runtime_damage.cpp /Fo"%BUILD%\test_damage.obj" /Fe"%BUILD%\test_damage.exe" || exit /b 1
"%BUILD%\test_damage.exe" || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_link_runtime_trace.cpp /I"%MH%\include" /Fo"%BUILD%\test_trace.obj" /Fe"%BUILD%\test_trace.exe" /link "%BUILD%\minhook.lib" || exit /b 1
"%BUILD%\test_trace.exe" || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_link_runtime_log.cpp /Fo"%BUILD%\test_log.obj" /Fe"%BUILD%\test_log.exe" || exit /b 1
"%BUILD%\test_log.exe" "%BUILD%" || exit /b 1
rem Shot line parry: standalone x86 transport and admission regression.
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_parry_line.cpp /I"%MH%\include" /Fo"%BUILD%\test_parry_line.obj" /Fe"%BUILD%\test_parry_line.exe" /link "%BUILD%\minhook.lib" || exit /b 1
"%BUILD%\test_parry_line.exe" || exit /b 1
rem Direct parry: native contact shims and fixed-damage admission.
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_parry_direct_routes.cpp /I"%MH%\include" /Fo"%BUILD%\test_parry_direct_routes.obj" /Fe"%BUILD%\test_parry_direct_routes.exe" /link "%BUILD%\minhook.lib" || exit /b 1
"%BUILD%\test_parry_direct_routes.exe" || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_parry_direct_runtime.cpp /I"%MH%\include" /Fo"%BUILD%\test_parry_direct_runtime.obj" /Fe"%BUILD%\test_parry_direct_runtime.exe" /link "%BUILD%\minhook.lib" || exit /b 1
"%BUILD%\test_parry_direct_runtime.exe" || exit /b 1
rem VERSIONINFO (dll\link_runtime.rc; labels in dll\link_runtime_version.h), linked into the DLL.
"%WK%\bin\%WKVER%\x86\rc.exe" /nologo /fo "%BUILD%\link_runtime.res" dll\link_runtime.rc || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc /LD /DUNICODE /D_UNICODE dll\link_runtime.cpp /I"%MH%\include" /Fo"%BUILD%\link_runtime.obj" /link /DEF:dll\link_runtime.def /OUT:"%BUILD%\dinput8.dll" /IMPLIB:"%BUILD%\dinput8.lib" "%BUILD%\minhook.lib" "%BUILD%\link_runtime.res" kernel32.lib user32.lib winmm.lib || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc /DUNICODE /D_UNICODE tests\native\test_link_runtime_proxy.cpp /Fo"%BUILD%\test_proxy.obj" /Fe"%BUILD%\test_proxy.exe" /link dxguid.lib || exit /b 1
copy /y dll\link_runtime.ini "%BUILD%\link_runtime.ini" >nul
"%BUILD%\test_proxy.exe" "%BUILD%\dinput8.dll" || exit /b 1
echo READY: %BUILD%\dinput8.dll
