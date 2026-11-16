@echo off
cmake --build "%~dp0build" --clean-first --parallel 12
if errorlevel 1 exit /b %errorlevel%
cmake --install "%~dp0build" --prefix "%~dp0output"
exit /b %errorlevel%
