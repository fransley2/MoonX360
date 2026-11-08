@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-360stream.ps1"
if errorlevel 1 pause
