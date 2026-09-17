@echo off
setlocal
set "APP=%~dp0build\RaceEngineer.exe"
if not exist "%APP%" (
  echo RaceEngineer.exe was not found. Build the project first.
  pause
  exit /b 1
)
start "Race Engineer" "%APP%" %*
