@rem **************************************************************************
@rem WWIV Build Script.
@rem
@rem Required Variables:
@rem WORKSPACE - git root directory
@rem BUILD_NUMBER - Jenkins Build number
@rem
@rem Installed Software:
@rem   7-Zip [C:\Program Files\7-Zip\7z.exe]
@rem   Visual Studio [C:\Program Files (x86)\Microsoft Visual Studio\VER]
@rem   cmake [in PATH, set by vcvarsall.bat]
@rem 
@rem **************************************************************************

setlocal
@echo off

del wwiv-*.zip

if /I "%LABEL%"=="win-x86" (
	@echo "Setting x86 (32-bit) Architecture"
	set WWIV_ARCH=x86
)
if /I "%LABEL%"=="win-x64" (
	@echo "Setting x64 (64-bit) Architecture"
	set WWIV_ARCH=x64
)

set ZIP_EXE="C:\Program Files\7-Zip\7z.exe"
set WWIV_RELEASE=5.5.0
set WWIV_FULL_RELEASE=5.5.0.%BUILD_NUMBER%
set WWIV_RELEASE_ARCHIVE_FILE=wwiv-win-%WWIV_ARCH%-%WWIV_FULL_RELEASE%.zip
set CMAKE_BINARY_DIR=%WORKSPACE%\_build
set WWIV_RELEASE_DIR=%CMAKE_BINARY_DIR%\release
set WWIV_INSTALL_SRC=%WORKSPACE%\install
set VS_VERSION=2019
set VS_BUILDTOOLS_DIR=Microsoft Visual Studio\%VS_VERSION%\BuildTools\VC\Auxiliary\Build\
set VS_COMMUNITY_DIR=Microsoft Visual Studio\%VS_VERSION%\Community\VC\Auxiliary\Build\
set VS_PREVIEW_DIR=Microsoft Visual Studio\%VS_VERSION%\Preview\VC\Auxiliary\Build\

@rem ===============================================================================

@if exist "%ProgramFiles(x86)%\%VS_BUILDTOOLS_DIR%\vcvarsall.bat" (
  echo "%ProgramFiles(x86)%\%VS_BUILDTOOLS_DIR%\vcvarsall.bat" %WWIV_ARCH%
  call "%ProgramFiles(x86)%\%VS_BUILDTOOLS_DIR%\vcvarsall.bat" %WWIV_ARCH%
  set VS_EDITION="BuildTools"
  set VS_INSTALL_DIR=%VS_BUILDTOOLS_DIR%
)

@if exist "%ProgramFiles%\%VS_BUILDTOOLS_DIR%\vcvarsall.bat" (
  echo "%ProgramFiles%\%VS_BUILDTOOLS_DIR%\vcvarsall.bat" %WWIV_ARCH%
  call "%ProgramFiles%\%VS_BUILDTOOLS_DIR%\vcvarsall.bat" %WWIV_ARCH%
  set VS_EDITION="BuildTools"
  set VS_INSTALL_DIR=%VS_BUILDTOOLS_DIR%
)

@if exist "%ProgramFiles(x86)%\%VS_COMMUNITY_DIR%\vcvarsall.bat" (
  echo "%ProgramFiles(x86)%\%VS_COMMUNITY_DIR%\vcvarsall.bat" %WWIV_ARCH%
  call "%ProgramFiles(x86)%\%VS_COMMUNITY_DIR%\vcvarsall.bat" %WWIV_ARCH%
  set VS_EDITION="Community"
  set VS_INSTALL_DIR=%VS_COMMUNITY_DIR%
)

@if exist "%ProgramFiles%\%VS_COMMUNITY_DIR%\vcvarsall.bat" (
  echo "%ProgramFiles%\%VS_COMMUNITY_DIR%\vcvarsall.bat" %WWIV_ARCH%
  call "%ProgramFiles%\%VS_COMMUNITY_DIR%\vcvarsall.bat" %WWIV_ARCH%
  set VS_EDITION="Community"
  set VS_INSTALL_DIR=%VS_COMMUNITY_DIR%
)

@if exist "%ProgramFiles(x86)%\%VS_PREVIEW_DIR%\vcvarsall.bat" (
  echo "%ProgramFiles(x86)%\%VS_PREVIEW_DIR%\vcvarsall.bat" %WWIV_ARCH%
  call "%ProgramFiles(x86)%\%VS_PREVIEW_DIR%\vcvarsall.bat" %WWIV_ARCH%
  set VS_EDITION="Preview"
  set VS_INSTALL_DIR=%VS_PREVIEW_DIR%
)

@echo =============================================================================
@echo Workspace:            %WORKSPACE% 
@echo Label:                %LABEL%
@echo WWIV_ARCHitecture:    %WWIV_ARCH%
@echo WWIV Release:         %WWIV_RELEASE%        
@echo Build Number:         %BUILD_NUMBER%
@echo WWIV CMake Root:      %CMAKE_BINARY_DIR%
@echo WWIV_ARCHive:         %WWIV_RELEASE_ARCHIVE_FILE%
@echo Release Dir:          %WWIV_RELEASE_DIR%
@echo Visual Studio Ver:    %VS_VERSION%
@echo Visual Studio Ed:     %VS_EDITION%
@echo Visual Studio DIR:    %VS_INSTALL_DIR%
@echo WindowsSdkVerBinPath  %WindowsSdkVerBinPath%
@echo WindowsLibPath        %WindowsLibPath%
@echo INCLUDE               %INCLUDE%
@echo =============================================================================

if not exist %CMAKE_BINARY_DIR% (
  echo Creating %CMAKE_BINARY_DIR%
  mkdir %CMAKE_BINARY_DIR%
)
del %CMAKE_BINARY_DIR%\CMakeCache.txt

rmdir /s/q %CMAKE_BINARY_DIR%\CMakeFiles
cd %WORKSPACE%
if not exist %WWIV_RELEASE_DIR% (
  echo Creating %WWIV_RELEASE_DIR%
  mkdir %WWIV_RELEASE_DIR%
)
del /q %WWIV_RELEASE_DIR%
del wwiv-*.zip

rem Turn echo back on now.
@echo on

echo * Building WWIV
cd %CMAKE_BINARY_DIR%
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ^
    -DWWIV_RELEASE=%WWIV_RELEASE% ^
    -DWWIV_ARCH=%WWIV_ARCH%  ^
    -DWWIV_BUILD_NUMBER=%BUILD_NUMBER% ^
    %WORKSPACE% || exit /b
cmake --build . --config Release || exit /b

@echo =============================================================================
@echo                           **** RUNNING TESTS ****
@echo =============================================================================
ctest --no-compress-output --output-on-failure -T Test 

echo * Creating release Archive: %WWIV_RELEASE_ARCHIVE_FILE%
cpack -G ZIP || exit /b 

cd %WORKSPACE%
copy /y/v %CMAKE_BINARY_DIR%\%WWIV_RELEASE_ARCHIVE_FILE% %WORKSPACE%\%WWIV_RELEASE_ARCHIVE_FILE%

echo **** SUCCESS ****
echo ** Archive File: %WWIV_RELEASE_ARCHIVE_FILE%
echo ** Archive contents:
%ZIP_EXE% l %WORKSPACE%\%WWIV_RELEASE_ARCHIVE_FILE%
endlocal
