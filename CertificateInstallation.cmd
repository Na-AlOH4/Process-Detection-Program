@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion


set CERT_FILE1=LanChatProgram.cer
set CERT_FILE2=ProcessDetectionProgram.cer


:: 检查是否已拥有管理员权限
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [提示] 需要管理员权限，正在请求提升...
    :: 使用 PowerShell 以管理员身份重新运行本脚本
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

:: 切换到脚本所在目录
cd /d "%~dp0"

:: 检查证书文件是否存在
if not exist "%CERT_FILE1%" (
    echo [错误] 找不到文件: %CERT_FILE1%
    pause
    exit /b 1
)
if not exist "%CERT_FILE2%" (
    echo [错误] 找不到文件: %CERT_FILE2%
    pause
    exit /b 1
)

echo.
echo 正在使用 certutil 导入证书到"受信任的根证书颁发机构"...
echo ================================================

:: 导入第一个证书
echo [1/2] 正在导入 %CERT_FILE1% ...
certutil -addstore -f "Root" "%CERT_FILE1%"
if %errorlevel% equ 0 (
    echo 成功: %CERT_FILE1% 已导入。
) else (
    echo 失败: %CERT_FILE1% 导入失败，错误代码: %errorlevel%
)

:: 导入第二个证书
echo.
echo [2/2] 正在导入 %CERT_FILE2% ...
certutil -addstore -f "Root" "%CERT_FILE2%"
if %errorlevel% equ 0 (
    echo 成功: %CERT_FILE2% 已导入。
) else (
    echo 失败: %CERT_FILE2% 导入失败，错误代码: %errorlevel%
)

echo.
echo ================================================
echo 操作完成。
pause