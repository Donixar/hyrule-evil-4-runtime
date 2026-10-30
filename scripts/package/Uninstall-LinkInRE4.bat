@echo off
rem Link in RE4 - desinstalador / uninstaller. Ejecuta uninstall.ps1 con Windows PowerShell 5.1.
rem Opciones / options: -GamePath "carpeta del juego"  -Yes
rem One line on purpose: uninstall.ps1 deletes the copy of this file kept in LinkInRE4_Backup; that copy first
rem steps out of the folder so it can be removed (the package copy keeps the caller's folder for -GamePath).
(for %%I in ("%~dp0.") do @if /i "%%~nxI"=="LinkInRE4_Backup" cd /d "%%~dpI") & "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1" %* & call set "LIR4_RC=%%errorlevel%%" & pause & (goto) 2>nul & call exit /b %%LIR4_RC%%
