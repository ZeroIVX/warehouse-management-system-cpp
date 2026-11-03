@echo off
chcp 936 >nul
setlocal EnableExtensions EnableDelayedExpansion
color 07
title 仓库管理系统 v2 - 构建与运行

pushd "%~dp0"

set "EXE=仓库管理系统.exe"
set "TESTS=wh_tests.exe"
set "BENCH=wh_bench.exe"
set "OBJDIR=obj"
set "MODE=run"
set "FORCE=0"

rem ============================================================
rem  用法：
rem    双击 / 无参数   源码有变动则重新编译，然后启动主程序
rem    -r  rebuild    强制重新编译
rem    -t  test       编译并运行单元测试
rem    -p  bench      编译并运行性能基准
rem    -b  build      只编译主程序，不运行
rem ============================================================
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-r"      set "FORCE=1"
if /i "%~1"=="rebuild" set "FORCE=1"
if /i "%~1"=="-t"      set "MODE=test"
if /i "%~1"=="test"    set "MODE=test"
if /i "%~1"=="-p"      set "MODE=bench"
if /i "%~1"=="bench"   set "MODE=bench"
if /i "%~1"=="-b"      set "MODE=build"
if /i "%~1"=="build"   set "MODE=build"
shift
goto parse_args
:args_done

rem ---------- 1. 收集源文件 ----------
set "SRCS="
for %%F in ("src\*.cpp") do set "SRCS=!SRCS! "%%~fF""
if not defined SRCS (
    echo [错误] 当前目录下没有找到 src\*.cpp 源文件。
    echo        脚本目录：%CD%
    goto failed
)

rem ---------- 2. 判断产物是否过期 ----------
set "NEWEST="
for %%F in (src\*.cpp include\wh\*.h tests\*.cpp bench\*.cpp) do (
    if "%%~tF" GTR "!NEWEST!" set "NEWEST=%%~tF"
)

rem 按本次目标选择要检查的产物，否则 -t / -p 会被主程序的时间戳误判为"无需编译"
set "TARGET=%EXE%"
if /i "%MODE%"=="test"  set "TARGET=%TESTS%"
if /i "%MODE%"=="bench" set "TARGET=%BENCH%"

if "%FORCE%"=="1" goto do_build
if not exist "%TARGET%" goto do_build
for %%F in ("%TARGET%") do set "TARGETTIME=%%~tF"
if "!NEWEST!" GTR "!TARGETTIME!" goto do_build
echo [跳过编译] 产物已是最新（加 -r 可强制重编）
goto dispatch

rem ---------- 3. 编译 ----------
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

set "CLFLAGS=/nologo /std:c++17 /EHsc /O2 /MD /utf-8 /W3 /Iinclude /DUNICODE /D_UNICODE"

echo.
echo === 编译主程序 ===
cl %CLFLAGS% !SRCS! /Fe:"%EXE%" /Fo"%OBJDIR%\\"
if errorlevel 1 goto build_failed

if /i "%MODE%"=="test" (
    echo.
    echo === 编译单元测试 ===
    cl %CLFLAGS% tests\test_main.cpp src\console.cpp src\repository.cpp src\service.cpp /Fe:"%TESTS%" /Fo"%OBJDIR%\\"
    if errorlevel 1 goto build_failed
)

if /i "%MODE%"=="bench" (
    echo.
    echo === 编译性能基准 ===
    cl %CLFLAGS% bench\bench.cpp src\console.cpp /Fe:"%BENCH%" /Fo"%OBJDIR%\\"
    if errorlevel 1 goto build_failed
)

echo.
echo [完成] 编译成功

rem ---------- 4. 分派 ----------
:dispatch
if /i "%MODE%"=="test" (
    if not exist "%TESTS%" goto missing_artifact
    echo.
    echo ============================================================
    echo   运行单元测试
    echo ============================================================
    "%TESTS%"
    if errorlevel 1 (
        echo.
        echo [失败] 存在未通过的测试用例
        goto failed
    )
    echo.
    echo [通过] 全部断言通过
    goto success
)

if /i "%MODE%"=="bench" (
    if not exist "%BENCH%" goto missing_artifact
    echo.
    "%BENCH%"
    goto success
)

if /i "%MODE%"=="build" goto success

echo.
echo ============================================================
echo   启动仓库管理系统
echo ============================================================
echo.
"%EXE%"
echo.
echo [程序已退出]
goto success

rem ---------- 异常出口 ----------
:missing_artifact
echo [错误] 目标程序不存在，请加 -r 重新编译。
goto failed

:no_compiler
echo [错误] 未找到 C++ 编译器。
echo        请安装 Visual Studio 并勾选「使用 C++ 的桌面开发」工作负载，
echo        或把 MinGW / TDM-GCC 的 bin 目录加入 PATH 后重试。
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
