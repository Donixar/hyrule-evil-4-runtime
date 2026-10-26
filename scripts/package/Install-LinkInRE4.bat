@echo off
rem Link in RE4 - instalador / installer. Ejecuta install.ps1 con Windows PowerShell 5.1.
rem Opciones / options: -GamePath "carpeta del juego"  -Parts core,sd,hd  -Yes  -DryRun
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %* & call set "LIR4_RC=%%errorlevel%%" & pause & (goto) 2>nul & call exit /b %%LIR4_RC%%
