@echo off
chcp 936 >nul
setlocal EnableExtensions EnableDelayedExpansion
color 07
title 仓库管理系统 v1（原始版）- 构建与运行

rem ============================================================
rem  说明：本版本为课程提交时的原始实现，仅做编码转换（GBK -> UTF-8 BOM）。
rem        编译时【不要】加 /utf-8：
rem        MSVC 检测到 BOM 会按 UTF-8 读源码，但执行字符集仍为系统 ANSI(GBK)，
rem        这样二进制里的中文是 GBK，与程序内部的 SetConsoleOutputCP(CP_ACP) 一致。
rem        若加 /utf-8，字面量会变成 UTF-8 而控制台是 936，中文反而乱码。
rem
rem  用法：
rem    双击 / 无参数   需要时编译，然后启动
rem    -r  rebuild    强制重新编译
rem    -b  build      只编译不运行
rem ============================================================

pushd "%~dp0"

set "SRC=src\warehouse_v1.cpp"
set "EXE=warehouse_v1.exe"
set "OBJDIR=obj"
set "BUILDONLY=0"
set "FORCE=0"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-b"      set "BUILDONLY=1"
if /i "%~1"=="build"   set "BUILDONLY=1"
if /i "%~1"=="-r"      set "FORCE=1"
if /i "%~1"=="rebuild" set "FORCE=1"
shift
goto parse_args
:args_done

if not exist "%SRC%" (
    echo [错误] 没有找到 %SRC%
    echo        当前目录：%CD%
    goto failed
)
echo [源文件] %SRC%

if "%FORCE%"=="1" goto do_build
if not exist "%EXE%" goto do_build
for %%F in ("%SRC%") do set "SRCTIME=%%~tF"
for %%F in ("%EXE%") do set "EXETIME=%%~tF"
if "!SRCTIME!" GTR "!EXETIME!" goto do_build
echo [跳过编译] 产物已是最新（加 -r 可强制重编）
goto dispatch

:do_build
echo.
echo ============================================================
echo   准备编译环境 ...
echo ============================================================

set "VSW="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSW=%%i"
if defined VSW (
    if exist "!VSW!\VC\Auxiliary\Build\vcvars64.bat" call "!VSW!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
)

where cl >nul 2>&1
if errorlevel 1 goto no_compiler
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

echo.
echo === 编译 ===
cl /nologo /std:c++17 /EHsc /O2 /MD /W3 /DUNICODE /D_UNICODE "%SRC%" /Fe:"%EXE%" /Fo"%OBJDIR%\\"
if errorlevel 1 goto build_failed
echo.
echo [完成] 编译成功：%EXE%

:dispatch
if "%BUILDONLY%"=="1" goto success

echo.
echo ============================================================
echo   启动仓库管理系统 v1
echo ============================================================
echo.
"%EXE%"
echo.
echo [程序已退出]
goto success

:no_compiler
echo [错误] 未找到 C++ 编译器。
echo        请安装 Visual Studio 并勾选「使用 C++ 的桌面开发」工作负载。
goto failed

:build_failed
echo.
echo [错误] 编译失败，请查看上面的编译输出。

:failed
echo.
echo 按任意键关闭窗口 ...
pause >nul
popd
endlocal
exit /b 1

:success
echo.
echo 按任意键关闭窗口 ...
pause >nul
popd
endlocal
exit /b 0
