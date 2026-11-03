@echo off
setlocal
rem Compile only; never install or start the game. Requires the local x86 VS toolchain.
if not defined VCVARS32 set "VCVARS32=N:\VSBuildTools\VC\Auxiliary\Build\vcvars32.bat"
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
rem Validate the approved delivery settings before producing or copying artifacts.
python -m pytest tests\test_runtime_release_config.py -q || exit /b 1
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
rem Image-dependent pin checks (hunt-findings P2-3, 1-nov): the bolt and direct-route fixtures and the secondary slot
rem check their native pins against the pinned mapped image (SHA bb59e550...). LINK_PIN_IMAGE names it, else the default
rem work copy below. A named image that is missing fails the build; with none the pure checks still run and the build
rem says the pin checks were skipped.
set "PIN_IMAGE=%LINK_PIN_IMAGE%"
if not defined PIN_IMAGE if exist "D:\link-in-re4-work\dll-poc-build-shield\bio4-memory-image.bin" set "PIN_IMAGE=D:\link-in-re4-work\dll-poc-build-shield\bio4-memory-image.bin"
if defined PIN_IMAGE if not exist "%PIN_IMAGE%" (echo ERROR: pinned image "%PIN_IMAGE%" not found& exit /b 1)
if not defined PIN_IMAGE echo WARNING: no pinned mapped image: bolt, direct-route and slot pin checks skipped ^(set LINK_PIN_IMAGE^)
rem Shot line parry: standalone x86 transport and admission regression.
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_parry_line.cpp /I"%MH%\include" /Fo"%BUILD%\test_parry_line.obj" /Fe"%BUILD%\test_parry_line.exe" /link "%BUILD%\minhook.lib" || exit /b 1
if defined PIN_IMAGE ("%BUILD%\test_parry_line.exe" "%PIN_IMAGE%" || exit /b 1) else ("%BUILD%\test_parry_line.exe" || exit /b 1)
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_parry_pushback.cpp /Fo"%BUILD%\test_parry_pushback.obj" /Fe"%BUILD%\test_parry_pushback.exe" || exit /b 1
"%BUILD%\test_parry_pushback.exe" || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_parry_observe_log.cpp /Fo"%BUILD%\test_parry_observe_log.obj" /Fe"%BUILD%\test_parry_observe_log.exe" || exit /b 1
"%BUILD%\test_parry_observe_log.exe" || exit /b 1
rem Direct parry: native contact shims and fixed-damage admission.
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_parry_direct_routes.cpp /I"%MH%\include" /Fo"%BUILD%\test_parry_direct_routes.obj" /Fe"%BUILD%\test_parry_direct_routes.exe" /link "%BUILD%\minhook.lib" || exit /b 1
if defined PIN_IMAGE ("%BUILD%\test_parry_direct_routes.exe" "%PIN_IMAGE%" || exit /b 1) else ("%BUILD%\test_parry_direct_routes.exe" || exit /b 1)
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_parry_direct_runtime.cpp /I"%MH%\include" /Fo"%BUILD%\test_parry_direct_runtime.obj" /Fe"%BUILD%\test_parry_direct_runtime.exe" /link "%BUILD%\minhook.lib" || exit /b 1
"%BUILD%\test_parry_direct_runtime.exe" || exit /b 1
rem slot-guard-01: player+2A8 hand-back, watchdog and arm probe (dll\secondary_slot.h).
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc tests\native\test_secondary_slot.cpp /Fo"%BUILD%\test_secondary_slot.obj" /Fe"%BUILD%\test_secondary_slot.exe" || exit /b 1
if defined PIN_IMAGE ("%BUILD%\test_secondary_slot.exe" "%PIN_IMAGE%" || exit /b 1) else ("%BUILD%\test_secondary_slot.exe" || exit /b 1)
rem VERSIONINFO (dll\link_runtime.rc; labels in dll\link_runtime_version.h), linked into the DLL.
"%WK%\bin\%WKVER%\x86\rc.exe" /nologo /fo "%BUILD%\link_runtime.res" dll\link_runtime.rc || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc /LD /DUNICODE /D_UNICODE /experimental:deterministic /pathmap:"%CD%"=\hyrule-evil-4-runtime dll\link_runtime.cpp /I"%MH%\include" /Fo"%BUILD%\link_runtime.obj" /link /DEF:dll\link_runtime.def /OUT:"%BUILD%\dinput8.dll" /IMPLIB:"%BUILD%\dinput8.lib" "%BUILD%\minhook.lib" "%BUILD%\link_runtime.res" kernel32.lib user32.lib winmm.lib || exit /b 1
cl /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc /DUNICODE /D_UNICODE tests\native\test_link_runtime_proxy.cpp /Fo"%BUILD%\test_proxy.obj" /Fe"%BUILD%\test_proxy.exe" /link dxguid.lib || exit /b 1
copy /y dll\link_runtime.ini "%BUILD%\link_runtime.ini" >nul
"%BUILD%\test_proxy.exe" "%BUILD%\dinput8.dll" || exit /b 1
echo READY: %BUILD%\dinput8.dll
