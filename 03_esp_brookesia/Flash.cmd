@echo off
title VIEWE - Flash Demo
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0demo.ps1" -Action flash
pause
