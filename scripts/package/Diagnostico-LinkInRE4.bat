@echo off
setlocal
title Diagnostico de Link in RE4
echo Recopilando diagnostico. No se modifica el juego ni se inicia RE4.
echo Puede tardar unos minutos mientras se comprueban los archivos instalados.
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0diagnose.ps1" %*
set "diagnostic_exit=%errorlevel%"
echo.
if not "%diagnostic_exit%"=="0" echo No se pudo completar el ZIP. Copia el error de esta ventana para Donald.
pause
exit /b %diagnostic_exit%
