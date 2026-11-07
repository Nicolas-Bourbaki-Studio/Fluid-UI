@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "SOURCE_DIR=%ROOT_DIR%src"
set "BUILD_DIR=%ROOT_DIR%build"
set "OUTPUT_DIR=%ROOT_DIR%output"

set "CLEAN_BUILD=false"
set "CLEAN_BUILD_AFTER_COPYING=false"
set "BUILD_TYPE=Debug"
set "CPP_STD=20"

if /I "%CLEAN_BUILD%"=="true" (
    if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
    REM if exist "%OUTPUT_DIR%" rmdir /S /Q "%OUTPUT_DIR%"
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
REM if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

pushd "%BUILD_DIR%"

conan install .. --output-folder=. --build=missing ^
    -s:h build_type=%BUILD_TYPE% -s:h compiler.cppstd=%CPP_STD% ^
    -s:b build_type=%BUILD_TYPE% -s:b compiler.cppstd=%CPP_STD%
if errorlevel 1 goto :fail

cmake .. ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_TOOLCHAIN_FILE="%CD%\conan_toolchain.cmake" ^
    -DCMAKE_C_COMPILER=gcc ^
    -DCMAKE_CXX_COMPILER=g++ ^
    -DCMAKE_CXX_STANDARD=%CPP_STD%
if errorlevel 1 goto :fail

cmake --build . -j
if errorlevel 1 goto :fail

REM cmake --install . --prefix "%OUTPUT_DIR%"
REM if errorlevel 1 goto :fail

popd

if /I "%CLEAN_BUILD_AFTER_COPYING%"=="true" (
    if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
)

echo Build succeeded.
endlocal
exit /b 0

:fail
echo Build FAILED.
popd 2>nul
endlocal
exit /b 1