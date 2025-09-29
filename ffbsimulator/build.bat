#==============================================================================
# build.bat (Script de compilation rapide)
#==============================================================================

@echo off
echo Compilation du simulateur Force Feedback...

REM Recherche automatique de cl.exe
where cl.exe >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Recherche de Visual Studio...
    call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    if %ERRORLEVEL% NEQ 0 (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
        if %ERRORLEVEL% NEQ 0 (
            echo Erreur: Visual Studio non trouvé!
            pause
            exit /b 1
        )
    )
)

REM Compilation
cl.exe /EHsc /std:c++17 /O2 /Fe:FFB_Simulator.exe FFB_Simulator.cpp dinput8.lib dxguid.lib user32.lib kernel32.lib

if %ERRORLEVEL% EQU 0 (
    echo Compilation réussie!
    echo Executable: FFB_Simulator.exe
    
    REM Test automatique si le volant est connecté
    echo.
    echo Test de détection du volant...
    FFB_Simulator.exe --test-device 2>nul
    
    echo.
    echo Appuyez sur une touche pour lancer le simulateur...
    pause >nul
    FFB_Simulator.exe
) else (
    echo Erreur de compilation!
    pause
)
