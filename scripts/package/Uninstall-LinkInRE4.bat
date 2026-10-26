@echo off
rem Link in RE4 - desinstalador / uninstaller. Ejecuta uninstall.ps1 con Windows PowerShell 5.1.
rem Opciones / options: -GamePath "carpeta del juego"  -Yes
rem One line on purpose: uninstall.ps1 deletes the copy of this file kept in LinkInRE4_Backup.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1" %* & call set "LIR4_RC=%%errorlevel%%" & pause & (goto) 2>nul & call exit /b %%LIR4_RC%%
