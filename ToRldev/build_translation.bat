@echo off
setlocal enabledelayedexpansion

:: Switch to script folder
cd /d "%~dp0"

echo ======================================================
echo  1/3. Inserting translation from Excel into .utf...
echo ======================================================
if not exist "oniuta.xlsx" (
    echo [ERROR] Excel file "oniuta.xlsx" not found!
    pause
    exit /b 1
)

py -3.12 utfdump.py from-xlsx Seen3 oniuta.xlsx Seen3
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to insert translation from Excel.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ======================================================
echo  2/3. Compiling scenes and updating SEEN.TXT...
echo ======================================================
cd Seen3
if exist *.TXT del /q *.TXT

py -3.12 ../rlc.py -e utf8 --force-transform -G ONIU -i ../GAMEEXE.INI *.org
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed.
    cd ..
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Packing archive SEEN.TXT...
py -3.12 ../kprl.py -a -G ONIU ../SEEN.TXT Seen*.TXT
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to create SEEN.TXT archive.
    cd ..
    pause
    exit /b %ERRORLEVEL%
)

cd ..

echo.
echo ======================================================
echo  3/3. Copying SEEN.TXT to game directory...
echo ======================================================
if exist "..\SEEN.TXT" (
    copy /y "..\SEEN.TXT" "..\SEEN.TXT.bak" >nul
)
copy /y "SEEN.TXT" "..\SEEN.TXT"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to copy SEEN.TXT to the game root folder.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ======================================================
echo  Done! Translation successfully applied to game folder.
echo ======================================================
pause
