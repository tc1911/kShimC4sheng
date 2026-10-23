@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
cd /d "%~dp0.."

where py >nul 2>nul
if %errorlevel%==0 (set "PY=py -3") else (set "PY=python")
%PY% -c "import sys" >nul 2>nul
if errorlevel 1 (
  echo [ERROR] 未找到 Python 3。请安装 https://www.python.org/downloads/windows/
  pause
  exit /b 1
)

if "%~1"=="" (
  set "STOCK=backup\boot_a.img"
  if not exist "!STOCK!" set "STOCK=out\backup\boot_a.img"
  set "DISTRO=build\debian\boot.img"
) else if "%~2"=="" (
  set "STOCK=backup\boot_a.img"
  if not exist "!STOCK!" set "STOCK=out\backup\boot_a.img"
  set "DISTRO=%~1"
) else (
  set "STOCK=%~1"
  set "DISTRO=%~2"
)

if not exist "!STOCK!" (
  echo [ERROR] 找不到原厂 boot_a.img：!STOCK!
  echo 用法：把原厂 boot_a.img 和发行版 boot.img 一起拖到本文件上；
  echo 或者：一键构建.bat <stock_boot.img> <distro_boot.img> [输出文件]
  pause
  exit /b 1
)
if not exist "!DISTRO!" (
  echo [ERROR] 找不到发行版 boot.img：!DISTRO!
  pause
  exit /b 1
)

set "OUT=out\boot_a_win.img"
if not "%~3"=="" set "OUT=%~3"

echo 原厂 boot : !STOCK!
echo 发行版 boot: !DISTRO!
echo 输出       : !OUT!
%PY% windows\build.py --stock-boot "!STOCK!" --distro-boot "!DISTRO!" --out "!OUT!"
if errorlevel 1 (
  echo.
  echo [FAILED] 构建失败，见上面的错误信息。
  pause
  exit /b 1
)
echo.
echo [OK] 已生成 !OUT!
echo 刷入：fastboot flash boot_a !OUT!
pause
