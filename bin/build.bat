@echo off

echo ========================================
echo Copyright (C) 2026 - Peneaux Benjamin 
echo This program is free software; 
echo you may redistribute and/or modify it under 
echo the terms of the GNU General Public License 
echo as published by the Free Software Foundation; 
echo either version 3 of the license.
echo - GNU GENERAL PUBLIC LICENSE V3 -
echo ========================================
echo
echo ========================================
echo ST32X Console
echo ========================================
echo.

echo Remove : output.bin...
del output.bin
echo Removed.

echo [1/3] Assembler compilation...
gcc ..\src\assembler.c -o ..\bin\st32x_asm -lws2_32
if errorlevel 1 (
    echo ERROR: asm.exe compilation failed
    pause
    exit /b 1
)
echo ✓ st32x_asm.exe compiled.

echo.
echo [2/3] input.asm assembling...
st32x_asm ..\test\input.asm ..\bin\output.bin
if errorlevel 1 (
    echo ERROR: game.asm assembling failed 
    pause
    exit /b 1
)
echo output.bin generated.

echo.
echo [3/3] Console ST32X compilation ...
gcc -o st32x_console ..\src\main.c ..\src\cpu.c ..\src\gpu.c ..\src\apu.c ..\src\controller.c -lSDL2 -lm -O2 -Wall
if errorlevel 1 (
    echo ERROR: st32x_console.exe compilation failed. 
    pause
    exit /b 1
)
echo st32x_console.exe compiled.

echo.
echo ========================================
echo COMPILATION DONE!
echo ========================================
echo.
echo Size of output.bin:
for %%F in (output.bin) do echo   %%~zF octets
echo.
echo output.bin hexdump creation...
del output.hex
certutil -encodehex output.bin output.hex
echo output.bin hexadump display...
type output.hex
echo.
echo Starting: st32x_console.exe
st32x_console.exe
echo.
pause
