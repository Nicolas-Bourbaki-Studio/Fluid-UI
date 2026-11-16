@echo off
set "ROOT=%~dp0.."
cmake --build "%ROOT%\build" --clean-first --parallel 12
if errorlevel 1 exit /b %errorlevel%
cmake --install "%ROOT%\build" --prefix "%ROOT%\output"
exit /b %errorlevel%
