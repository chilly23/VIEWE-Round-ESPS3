@echo off
title VIEWE - Build Demo
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0demo.ps1" -Action build
pause
