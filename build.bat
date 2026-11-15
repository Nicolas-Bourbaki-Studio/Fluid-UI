@echo off
cmake --build build --clean-first --parallel 12
exit /b %errorlevel%
