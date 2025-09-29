# build.ps1 - Script PowerShell pour compiler FFB Simulator
Write-Host "============================================" -ForegroundColor Cyan
Write-Host " Compilation FFB Simulator (PowerShell)" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

# Chemins
$vcvarsPath = "D:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$sourceFile = "FFB_Simulator.cpp"

# Vérifications
if (-not (Test-Path $vcvarsPath)) {
    Write-Host "ERREUR: vcvars64.bat non trouvé: $vcvarsPath" -ForegroundColor Red
    Read-Host "Appuyez sur Entrée pour continuer"
    exit 1
}

if (-not (Test-Path $sourceFile)) {
    Write-Host "ERREUR: $sourceFile non trouvé!" -ForegroundColor Red
    Write-Host "Répertoire courant: $(Get-Location)" -ForegroundColor Yellow
    Read-Host "Appuyez sur Entrée pour continuer"
    exit 1
}

Write-Host "✓ vcvars64.bat trouvé" -ForegroundColor Green
Write-Host "✓ Code source trouvé: $sourceFile" -ForegroundColor Green
Write-Host ""

# Création d'un script temporaire pour initialiser l'environnement et compiler
$tempBat = [System.IO.Path]::GetTempFileName() + ".bat"

$batContent = @"
@echo off
call "$vcvarsPath"
if %ERRORLEVEL% NEQ 0 (
    echo ERREUR: Impossible d'initialiser l'environnement Visual Studio
    exit /b 1
)

echo Compilation en cours...
cl.exe /EHsc /std:c++17 /O2 /MD /Fe:FFB_Simulator.exe FFB_Simulator.cpp dinput8.lib dxguid.lib user32.lib kernel32.lib
exit /b %ERRORLEVEL%
"@

try {
    # Écriture du script temporaire
    $batContent | Out-File -FilePath $tempBat -Encoding ASCII
    
    # Exécution
    Write-Host "Lancement de la compilation..." -ForegroundColor Yellow
    $process = Start-Process -FilePath $tempBat -WorkingDirectory (Get-Location) -Wait -PassThru -NoNewWindow
    
    # Nettoyage
    Remove-Item $tempBat -Force
    
    if ($process.ExitCode -eq 0) {
        Write-Host ""
        Write-Host "============================================" -ForegroundColor Green
        Write-Host " COMPILATION RÉUSSIE! 🎉" -ForegroundColor Green  
        Write-Host "============================================" -ForegroundColor Green
        
        if (Test-Path "FFB_Simulator.exe") {
            $fileInfo = Get-Item "FFB_Simulator.exe"
            Write-Host "Exécutable créé: $($fileInfo.Name) ($($fileInfo.Length) octets)" -ForegroundColor Green
            Write-Host ""
            
            Write-Host "IMPORTANT:" -ForegroundColor Yellow
            Write-Host "1. Connectez votre Microsoft Sidewinder Force Feedback Wheel" -ForegroundColor White
            Write-Host "2. Vérifiez dans Paramètres > Bluetooth et périphériques" -ForegroundColor White
            Write-Host "3. Testez le volant dans 'Contrôleurs de jeu' Windows" -ForegroundColor White
            Write-Host ""
            
            $choice = Read-Host "Voulez-vous lancer le simulateur maintenant? (o/n)"
            if ($choice -eq "o" -or $choice -eq "O") {
                Write-Host ""
                Write-Host "CONTRÔLES DU SIMULATEUR:" -ForegroundColor Cyan
                Write-Host "  ESPACE     = Jouer/Arrêter l'effet" -ForegroundColor White
                Write-Host "  N/P        = Effet suivant/précédent" -ForegroundColor White  
                Write-Host "  +/-        = Augmenter/Diminuer intensité" -ForegroundColor White
                Write-Host "  Flèches    = Direction et durée" -ForegroundColor White
                Write-Host "  S          = Arrêter tous les effets" -ForegroundColor White
                Write-Host "  H          = Aide" -ForegroundColor White
                Write-Host "  ESC        = Quitter" -ForegroundColor White
                Write-Host ""
                Read-Host "Appuyez sur Entrée pour lancer"
                
                Start-Process -FilePath ".\FFB_Simulator.exe" -Wait
            } else {
                Write-Host ""
                Write-Host "Pour lancer plus tard: .\FFB_Simulator.exe" -ForegroundColor Cyan
            }
        } else {
            Write-Host "ERREUR: Exécutable non créé malgré compilation réussie!" -ForegroundColor Red
        }
    } else {
        Write-Host ""
        Write-Host "============================================" -ForegroundColor Red
        Write-Host " ERREUR DE COMPILATION!" -ForegroundColor Red
        Write-Host "============================================" -ForegroundColor Red
        Write-Host "Code d'erreur: $($process.ExitCode)" -ForegroundColor Red
        Write-Host ""
        Write-Host "Causes possibles:" -ForegroundColor Yellow
        Write-Host "- Code source incorrect" -ForegroundColor White
        Write-Host "- Bibliothèques DirectInput manquantes" -ForegroundColor White
        Write-Host "- Conflits de versions SDK" -ForegroundColor White
    }
} catch {
    Write-Host "ERREUR lors de lexecution: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    # Nettoyage au cas où
    if (Test-Path $tempBat) {
        Remove-Item $tempBat -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
Read-Host "Appuyez sur Entree pour continuer"
