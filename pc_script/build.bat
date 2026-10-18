@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ============================================
echo  BindDeck - build de BindDeck.exe
echo ============================================

rem --- 1) Locate Python 3 ------------------------------------------------
set "PY=python"
%PY% --version >nul 2>&1
if not errorlevel 1 goto :have_python
set "PY=py -3"
%PY% --version >nul 2>&1
if not errorlevel 1 goto :have_python
echo [ERROR] Python 3 no encontrado en PATH.
goto :fail
:have_python
echo [INFO] Usando: %PY%

rem --- 2) PyInstaller must be installed ----------------------------------
%PY% -m PyInstaller --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PyInstaller no esta instalado.
    echo         Ejecuta:  %PY% -m pip install -r requirements.txt
    goto :fail
)
echo [INFO] PyInstaller:
%PY% -m PyInstaller --version

rem --- 3) firmware.bin (optional at runtime, needed by "Install Firmware") -
if not exist "firmware.bin" (
    if exist "..\.pio\build\esp32dev\firmware.bin" (
        echo [INFO] Copiando firmware.bin desde .pio\build\esp32dev ...
        copy /Y "..\.pio\build\esp32dev\firmware.bin" "firmware.bin" >nul
    ) else (
        echo [WARN] No hay firmware.bin: "Install Official Firmware" fallara.
    )
)

rem --- 4) Remove stale output so a failed build cannot look successful ---
if exist "dist\BindDeck.exe" del /Q "dist\BindDeck.exe"
if exist "build\BindDeck" rmdir /S /Q "build\BindDeck"

echo.
echo [1/2] Empaquetando con PyInstaller (BindDeck.spec)...
%PY% -m PyInstaller --noconfirm --clean "BindDeck.spec"
if errorlevel 1 (
    echo.
    echo [ERROR] PyInstaller fallo. Revisa los mensajes de arriba.
    goto :fail
)

echo.
echo [2/2] Verificando dist\BindDeck.exe...
if not exist "dist\BindDeck.exe" (
    echo [ERROR] PyInstaller no genero dist\BindDeck.exe.
    goto :fail
)
set "EXESIZE=0"
for %%F in ("dist\BindDeck.exe") do set "EXESIZE=%%~zF"
if "%EXESIZE%"=="0" (
    echo [ERROR] dist\BindDeck.exe esta vacio.
    goto :fail
)

echo.
echo ============================================
echo  OK: dist\BindDeck.exe  (%EXESIZE% bytes)
echo ============================================
endlocal
pause
exit /b 0

:fail
echo.
echo ============================================
echo  BUILD FALLIDO - no se genero un exe valido.
echo ============================================
endlocal
pause
exit /b 1
