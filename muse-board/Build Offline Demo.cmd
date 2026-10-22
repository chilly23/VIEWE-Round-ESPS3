@echo off
title Muse Offline Demo - VIEWE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0viewe.ps1" -Action build -Offline
pause
