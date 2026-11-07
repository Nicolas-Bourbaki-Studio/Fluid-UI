@echo off
call "%~dp0\deactivate_conanrunenv-debug-x86_64.bat"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] %~dp0\deactivate_conanrunenv-debug-x86_64.bat failed with code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)