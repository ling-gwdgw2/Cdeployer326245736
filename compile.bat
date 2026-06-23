@echo off
echo ========================================
echo        DEPLOYFLOW COMPILER ASSISTANT
echo ========================================
echo.

if exist "deployer.exe" (
    echo [Info] deployer.exe already exists. Removing old binary...
    del deployer.exe
)

if exist "tcc\tcc.exe" (
    echo [Info] Compiler already exists locally. Skipping download...
    goto compile
)

echo [1/2] Downloading Tiny C Compiler (TCC)...
powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri 'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip' -OutFile 'tcc.zip'"

if not exist "tcc.zip" (
    echo [Error] Failed to download TCC. Please check your internet connection.
    pause
    exit /b 1
)

echo [2/2] Extracting compiler...
if exist "tcc_temp" rmdir /s /q tcc_temp
powershell -NoProfile -Command "Expand-Archive -Path 'tcc.zip' -DestinationPath 'tcc_temp' -Force"
del tcc.zip

:: Restructure files to local tcc directory
if exist "tcc_temp\tcc" (
    move tcc_temp\tcc tcc
    rmdir /s /q tcc_temp
) else (
    move tcc_temp tcc
)

if not exist "tcc\tcc.exe" (
    echo [Error] Failed to extract compiler tools.
    pause
    exit /b 1
)

:compile
echo Compiling deployer.c to deployer.exe...
tcc\tcc.exe deployer.c -o deployer.exe

if exist "deployer.exe" (
    echo.
    echo ========================================
    echo  SUCCESS: deployer.exe generated!
    echo ========================================
    echo.
    echo You can now copy 'deployer.exe' and 'deploy.conf'
    echo to your web projects.
) else (
    echo [Error] Compilation failed.
    pause
    exit /b 1
)
echo.
