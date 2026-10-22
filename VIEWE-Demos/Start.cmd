@echo off
title VIEWE ROUND - Demo Selector
:menu
cls
echo VIEWE ROUND DEMOS
echo.
echo 1. LVGL demo
echo 2. ESP-Brookesia
echo 3. Immersive block
echo 4. Spectrum analyzer
echo 5. Mosaico FluidBox
echo 0. Exit
echo.
choice /c 123450 /n /m "Choose a demo to upload: "
if errorlevel 6 exit /b
if errorlevel 5 (call "%~dp0..\esp-mosaico-fluidbox-main\Flash.cmd" & goto menu)
if errorlevel 4 (call "%~dp0..\05_Spec_Analyzer\Flash.cmd" & goto menu)
if errorlevel 3 (call "%~dp0..\04_Immersive_block\Flash.cmd" & goto menu)
if errorlevel 2 (call "%~dp0..\03_esp_brookesia\Flash.cmd" & goto menu)
if errorlevel 1 (call "%~dp0..\02_lvgl_demo_v9\Flash.cmd" & goto menu)
goto menu
