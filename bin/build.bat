@echo off
echo ========================================
echo ST32X Console - Compilation Complete
echo ========================================
echo.

echo Supression de de: output.bin...
del output.bin
echo Supprimé.

echo [1/3] Compilation de l'assembleur...
gcc ..\src\assembler.c -o ..\bin\st32x_asm -lws2_32
if errorlevel 1 (
    echo ERREUR: Echec de compilation de asm.exe
    pause
    exit /b 1
)
echo ✓ st32x_asm.exe compilé.

echo.
echo [2/3] Assemblage de input.asm...
st32x_asm ..\test\input.asm ..\bin\output.bin
if errorlevel 1 (
    echo ERREUR: Echec d'assemblage de game.asm
    pause
    exit /b 1
)
echo ✓ output.bin generé.

echo.
echo [3/3] Compilation de la console ST32X...
gcc -o st32x_console ..\src\main.c ..\src\cpu.c ..\src\gpu.c ..\src\apu.c ..\src\controller.c -lSDL2 -lm -O2 -Wall
if errorlevel 1 (
    echo ERREUR: Echec de compilation de st32x_console.exe
    pause
    exit /b 1
)
echo ✓ st32x_console.exe compilé.

echo.
echo ========================================
echo COMPILATION REUSSIE!
echo ========================================
echo.
echo Taille de output.bin:
for %%F in (output.bin) do echo   %%~zF octets
echo.
echo Creation d'un hexdump de output.bin..
del output.hex
certutil -encodehex output.bin output.hex
echo Affichage du hexdump de output.bin..
type output.hex
echo.
echo Lancement de: st32x_console.exe
st32x_console.exe
echo.
pause