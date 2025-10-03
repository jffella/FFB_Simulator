//==============================================================================
// FFB_Simulator.cpp - Simulateur d'effets à retour de force pour Windows
// Compatible Microsoft Sidewinder Force Feedback Wheel
// Copyright (c) 2024
//==============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <Shlwapi.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <chrono>
#include <conio.h>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "Shlwapi.lib")

//==============================================================================
// CONSTANTES
//==============================================================================

// IDs Microsoft Sidewinder Force Feedback Wheel
const WORD SIDEWINDER_VID = 0x045E;
const WORD SIDEWINDER_PID = 0x0034;

// Paramètres de force
const LONG MAX_FORCE = 10000;          // Force maximale DirectInput
const DWORD EFFECT_DURATION = 2000;    // Durée par défaut (ms)
const DWORD INFINITE_DURATION = INFINITE;

// Refresh rate
const DWORD UPDATE_INTERVAL = 16;      // ~60 FPS

//==============================================================================
// CLASSE DE LOGGING
//==============================================================================

class Logger
{
private:
    std::ofstream m_LogFile;
    std::string m_LogFilename;
    bool m_IsOpen;
    
    std::string GetTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t_now);
        
        std::ostringstream oss;
        oss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
    
    // Helper pour construire le message à partir des arguments
    template<typename T>
    void BuildMessage(std::ostringstream& oss, T&& arg)
    {
        oss << std::forward<T>(arg);
    }
    
    template<typename T, typename... Args>
    void BuildMessage(std::ostringstream& oss, T&& first, Args&&... args)
    {
        oss << std::forward<T>(first);
        BuildMessage(oss, std::forward<Args>(args)...);
    }
    
public:
    Logger() : m_IsOpen(false) {}
    
    ~Logger()
    {
        Close();
    }
    
    bool Open(const std::string& filename)
    {
        m_LogFilename = filename;
        m_LogFile.open(filename, std::ios::out | std::ios::app);
        m_IsOpen = m_LogFile.is_open();
        
        if (m_IsOpen)
        {
            m_LogFile << "\n========================================\n";
            m_LogFile << "Session démarrée: " << GetTimestamp() << "\n";
            m_LogFile << "========================================\n";
            m_LogFile.flush();
        }
        
        return m_IsOpen;
    }
    
    void Close()
    {
        if (m_IsOpen)
        {
            m_LogFile << "========================================\n";
            m_LogFile << "Session terminée: " << GetTimestamp() << "\n";
            m_LogFile << "========================================\n\n";
            m_LogFile.close();
            m_IsOpen = false;
        }
    }
    
    // Méthode template principale
    template<typename... Args>
    void Log(const std::string& level, Args&&... args)
    {
        std::ostringstream oss;
        BuildMessage(oss, std::forward<Args>(args)...);
        
        std::string fullMessage = "[" + GetTimestamp() + "] [" + level + "] " + oss.str();
        
        // Affichage console
        std::cout << fullMessage << std::endl;
        
        // Écriture fichier
        if (m_IsOpen)
        {
            m_LogFile << fullMessage << std::endl;
            m_LogFile.flush();
        }
    }
    
    // Méthodes pratiques avec templates variadiques
    template<typename... Args>
    void Info(Args&&... args)
    {
        Log("INFO", std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void Warning(Args&&... args)
    {
        Log("WARN", std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void Error(Args&&... args)
    {
        Log("ERROR", std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void Debug(Args&&... args)
    {
        Log("DEBUG", std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void Success(Args&&... args)
    {
        Log("OK", std::forward<Args>(args)...);
    }
    
    // Helper pour formater en hexadécimal
    struct Hex
    {
        HRESULT value;
        explicit Hex(HRESULT v) : value(v) {}
    };
    
    std::string GetFilename() const { return m_LogFilename; }
};

// Surcharge de l'opérateur << pour Logger::Hex
inline std::ostream& operator<<(std::ostream& os, const Logger::Hex& hex)
{
    os << "0x" << std::hex << std::uppercase << hex.value << std::dec;
    return os;
}

// Instance globale du logger
static Logger g_Logger;

//==============================================================================
// CLASSE PRINCIPALE
//==============================================================================

class ForceEffectSimulator
{
private:
    // Interfaces DirectInput
    IDirectInput8* m_pDI;
    IDirectInputDevice8* m_pDevice;
    
    // État du périphérique
    DIJOYSTATE2 m_JoyState;
    bool m_bDeviceAcquired;
    
    // Mode d'affichage
    bool m_bShowingHelp;
    
    // Gestion des effets
    std::map<std::string, IDirectInputEffect*> m_Effects;
    std::vector<std::string> m_EffectNames;
    int m_CurrentEffectIndex;
    bool m_bEffectPlaying;
    
    // Thread de mise à jour
    std::thread m_UpdateThread;
    // Indique si la boucle principale est active
    bool m_bRunning;
    
    // Paramètres d'effet ajustables
    LONG m_ForceIntensity;
    DWORD m_EffectDuration;
    LONG m_EffectDirection;
    
public:
    ForceEffectSimulator();
    ~ForceEffectSimulator();
    
    /**
     * Initialise le simulateur (DirectInput, périphérique, effets).
     * @return true si succès.
     */
    bool Initialize();
    void Shutdown();
    void Run();
    
private:
    // Initialisation
    bool InitializeDirectInput();
    bool FindAndInitDevice();
    bool SetupCooperativeLevel(HWND hwnd);
    bool SetupDataFormat();
    bool SetupForceFeedback();
    
    // Gestion des effets
    bool CreateAllEffects();
    bool CreateConstantEffect(const std::string& name, LONG force, LONG direction = 0);
    bool CreatePeriodicEffect(const std::string& name, const GUID& effectType, 
                            DWORD magnitude, DWORD period, DWORD phase = 0);
    bool CreateRampEffect(const std::string& name, LONG startForce, LONG endForce);
    bool CreateConditionEffect(const std::string& name, const GUID& effectType,
                             LONG coefficient, LONG saturation);
    
    // Contrôle des effets
    void PlayCurrentEffect();
    void StopCurrentEffect();
    void StopAllEffects();
    void NextEffect();
    void PreviousEffect();
    void AdjustIntensity(int delta);
    void AdjustDirection(int delta);
    void AdjustDuration(int delta);
    
    // Mise à jour et affichage
    void UpdateLoop();
    void UpdateDeviceState();
    void DisplayStatus();
    void DisplayHelp();
    
    // Utilitaires
    static std::string FormatForce(LONG force);
    static std::string FormatDirection(LONG direction);
    static std::string FormatDuration(DWORD duration);
    void CleanupEffects();
    
    // Callback pour énumération des périphériques
    static BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext);
    
    // Callback pour énumération des objets du périphérique
    static BOOL CALLBACK EnumObjectsCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext);
};

//==============================================================================
// IMPLÉMENTATION
//==============================================================================

ForceEffectSimulator::ForceEffectSimulator()
    : m_pDI(nullptr)
    , m_pDevice(nullptr)
    , m_bDeviceAcquired(false)
    , m_bShowingHelp(false)
    , m_CurrentEffectIndex(0)
    , m_bEffectPlaying(false)
    , m_bRunning(false)
    , m_ForceIntensity(5000)
    , m_EffectDuration(EFFECT_DURATION)
    , m_EffectDirection(0)
{
    ZeroMemory(&m_JoyState, sizeof(m_JoyState));
}

ForceEffectSimulator::~ForceEffectSimulator()
{
    Shutdown();
}

bool ForceEffectSimulator::Initialize()
{
    g_Logger.Info("=== Simulateur Force Feedback DirectInput ===");
    g_Logger.Info("Initialisation...");
    
    if (!InitializeDirectInput())
    {
        g_Logger.Error("Impossible d'initialiser DirectInput");
        return false;
    }
    
    if (!FindAndInitDevice())
    {
        g_Logger.Error("Impossible de trouver le volant Sidewinder");
        return false;
    }
    
    if (!CreateAllEffects())
    {
        g_Logger.Error("Impossible de créer les effets force feedback");
        return false;
    }
    
    g_Logger.Success("Initialisation terminée avec succès!");
    g_Logger.Info("Effets disponibles: " + std::to_string(m_Effects.size()));
    
    return true;
}

bool ForceEffectSimulator::InitializeDirectInput()
{
    HRESULT hr = DirectInput8Create(GetModuleHandle(nullptr),
                                   DIRECTINPUT_VERSION,
                                   IID_IDirectInput8,
                                   (void**)&m_pDI,
                                   nullptr);
    
    if (FAILED(hr))
    {
        g_Logger.Error("DirectInput8Create failed: ", Logger::Hex(hr));
        return false;
    }
    
    return true;
}

BOOL CALLBACK ForceEffectSimulator::EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext)
{
    ForceEffectSimulator* pThis = static_cast<ForceEffectSimulator*>(pContext);
    
    // Vérification du VID/PID via le GUID du produit
    // Pour DirectInput, nous devons créer le device pour vérifier ses propriétés
    IDirectInputDevice8* pTempDevice = nullptr;
    HRESULT hr = pThis->m_pDI->CreateDevice(pdidInstance->guidInstance, &pTempDevice, nullptr);
    
    if (SUCCEEDED(hr))
    {
        // Récupération des propriétés du device
        DIPROPDWORD dipProp;
        dipProp.diph.dwSize = sizeof(DIPROPDWORD);
        dipProp.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        dipProp.diph.dwHow = DIPH_DEVICE;
        dipProp.diph.dwObj = 0;
        
        // Vérification du VID
        hr = pTempDevice->GetProperty(DIPROP_VIDPID, &dipProp.diph);
        if (SUCCEEDED(hr))
        {
            WORD vid = LOWORD(dipProp.dwData);
            WORD pid = HIWORD(dipProp.dwData);
            
            g_Logger.Debug("Device trouvé: ", pdidInstance->tszProductName, 
                          " (VID: ", Logger::Hex(vid), ", PID: ", Logger::Hex(pid), ")");
            
            if (vid == SIDEWINDER_VID && pid == SIDEWINDER_PID)
            {
                g_Logger.Success("Microsoft Sidewinder Force Feedback Wheel détecté!");
                
                // Gardons ce device
                pThis->m_pDevice = pTempDevice;
                pTempDevice = nullptr; // Évite la libération
                
                return DIENUM_STOP; // Arrête l'énumération
            }
        }
        
        if (pTempDevice)
        {
            pTempDevice->Release();
        }
    }
    
    return DIENUM_CONTINUE;
}

bool ForceEffectSimulator::FindAndInitDevice()
{
    // Énumération des joysticks force feedback
    HRESULT hr = m_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL,
                                   EnumJoysticksCallback,
                                   this,
                                   DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    
    if (FAILED(hr) || !m_pDevice)
    {
        g_Logger.Error("Aucun volant Sidewinder trouvé ou erreur d'énumération");
        return false;
    }
    
    // Configuration du format de données
    if (!SetupDataFormat())
    {
        return false;
    }
    
    // Configuration du niveau coopératif
    if (!SetupCooperativeLevel(GetConsoleWindow()))
    {
        return false;
    }
    
    // Énumération des objets du device pour debug
    m_pDevice->EnumObjects(EnumObjectsCallback, this, DIDFT_ALL);
    
    // Configuration force feedback
    if (!SetupForceFeedback())
    {
        return false;
    }
    
    // Acquisition du device
    hr = m_pDevice->Acquire();
    if (SUCCEEDED(hr))
    {
        m_bDeviceAcquired = true;
        g_Logger.Success("Device acquis avec succès");
    }
    else
    {
        g_Logger.Warning("Device non acquis (sera tenté plus tard)");
    }
    
    return true;
}

bool ForceEffectSimulator::SetupDataFormat()
{
    HRESULT hr = m_pDevice->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr))
    {
        g_Logger.Error("SetDataFormat failed: ", Logger::Hex(hr));
        return false;
    }
    return true;
}

bool ForceEffectSimulator::SetupCooperativeLevel(HWND hwnd)
{
    // DISCL_EXCLUSIVE nécessaire pour force feedback
    // DISCL_BACKGROUND pour permettre l'utilisation même si la fenêtre n'a pas le focus
    HRESULT hr = m_pDevice->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    if (FAILED(hr))
    {
        g_Logger.Error("SetCooperativeLevel failed: ", Logger::Hex(hr));
        return false;
    }
    return true;
}

BOOL CALLBACK ForceEffectSimulator::EnumObjectsCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext)
{
    ForceEffectSimulator* pThis = static_cast<ForceEffectSimulator*>(pContext);
    
    std::string ffStatus = (pdidoi->dwFlags & DIDOI_FFACTUATOR) ? " [Force Feedback]" : "";
    g_Logger.Debug("Objet: ", pdidoi->tszName, " (Type: ", Logger::Hex(pdidoi->dwType), ")", ffStatus);
    
    // Configuration des axes force feedback
    if (pdidoi->dwFlags & DIDOI_FFACTUATOR)
    {
        // Configuration de la plage de l'axe pour force feedback
        DIPROPRANGE diprg;
        diprg.diph.dwSize = sizeof(DIPROPRANGE);
        diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        diprg.diph.dwHow = DIPH_BYID;
        diprg.diph.dwObj = pdidoi->dwType;
        diprg.lMin = -MAX_FORCE;
        diprg.lMax = MAX_FORCE;
        
        HRESULT hr = pThis->m_pDevice->SetProperty(DIPROP_RANGE, &diprg.diph);
        if (FAILED(hr))
        {
            g_Logger.Warning("  Erreur config range pour ", pdidoi->tszName);
        }
    }
    
    return DIENUM_CONTINUE;
}

bool ForceEffectSimulator::SetupForceFeedback()
{
    // Vérification des capacités force feedback
    DIDEVCAPS caps;
    caps.dwSize = sizeof(DIDEVCAPS);
    
    HRESULT hr = m_pDevice->GetCapabilities(&caps);
    if (FAILED(hr))
    {
        g_Logger.Error("GetCapabilities failed");
        return false;
    }
    
    g_Logger.Info("Capacités Force Feedback: Axes=", caps.dwAxes, 
                  ", FFDriverVersion=", caps.dwFFDriverVersion);
    
    // Activation de l'autocenter (important pour le volant)
    DIPROPDWORD dipProp;
    dipProp.diph.dwSize = sizeof(DIPROPDWORD);
    dipProp.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    dipProp.diph.dwHow = DIPH_DEVICE;
    dipProp.diph.dwObj = 0;
    dipProp.dwData = DIPROPAUTOCENTER_ON;
    
    hr = m_pDevice->SetProperty(DIPROP_AUTOCENTER, &dipProp.diph);
    if (FAILED(hr))
    {
        g_Logger.Warning("Impossible d'activer l'autocenter");
    }
    else
    {
        g_Logger.Info("Autocenter activé");
    }
    
    // Configuration du gain général
    dipProp.dwData = DI_FFNOMINALMAX; // Gain maximum
    hr = m_pDevice->SetProperty(DIPROP_FFGAIN, &dipProp.diph);
    if (FAILED(hr))
    {
        g_Logger.Warning("Impossible de configurer le gain");
    }
    else
    {
        g_Logger.Info("Gain configuré au maximum");
    }
    
    return true;
}

bool ForceEffectSimulator::CreateAllEffects()
{
    g_Logger.Info("Création des effets...");
    
    bool success = true;
    
    // Effets constants (forces plus importantes)
    success &= CreateConstantEffect("Constant_Droite", 8000, 0);
    success &= CreateConstantEffect("Constant_Gauche", -8000, 0);
    success &= CreateConstantEffect("Constant_Fort", 10000, 0);
    success &= CreateConstantEffect("Constant_Faible", 4000, 0);
    
    // Effets périodiques
    success &= CreatePeriodicEffect("Sinus", GUID_Sine, 6000, 200);
    success &= CreatePeriodicEffect("Carre", GUID_Square, 7000, 150);
    success &= CreatePeriodicEffect("Triangle", GUID_Triangle, 5000, 300);
    success &= CreatePeriodicEffect("Dent_Scie", GUID_SawtoothUp, 6000, 180);
    
    // Effets rampe (valeurs augmentées)
    success &= CreateRampEffect("Rampe_Montante", 2000, 10000);
    success &= CreateRampEffect("Rampe_Descendante", 10000, 2000);
    
    // Effets de condition (coefficient plus élevé)
    success &= CreateConditionEffect("Ressort", GUID_Spring, 8000, MAX_FORCE);
    success &= CreateConditionEffect("Amortissement", GUID_Damper, 7000, MAX_FORCE);
    success &= CreateConditionEffect("Inertie", GUID_Inertia, 6000, MAX_FORCE);
    success &= CreateConditionEffect("Friction", GUID_Friction, 5000, MAX_FORCE);
    
    g_Logger.Info("Effets créés: " + std::to_string(m_Effects.size()));
    
    // Construction de la liste des noms pour navigation
    for (const auto& pair : m_Effects)
    {
        m_EffectNames.push_back(pair.first);
    }
    
    return success && !m_Effects.empty();
}

bool ForceEffectSimulator::CreateConstantEffect(const std::string& name, LONG force, LONG direction)
{
    DIEFFECT eff;
    ZeroMemory(&eff, sizeof(eff));
    
    // Structure de base
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE_DURATION; // Les effets constants doivent être infinis
    eff.dwSamplePeriod = 0;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = 1; // Axe du volant uniquement
    
    // Configuration des axes
    DWORD axes = DIJOFS_X; // Axe principal du volant
    eff.rgdwAxes = &axes;
    
    // Direction en coordonnées cartésiennes (pas polaire)
    LONG directions = force; // La direction est intégrée dans la force (+ = droite, - = gauche)
    eff.rglDirection = &directions;
    
    // Paramètres spécifiques à l'effet constant
    DICONSTANTFORCE constantForce;
    constantForce.lMagnitude = abs(force); // Magnitude toujours positive
    
    eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
    eff.lpvTypeSpecificParams = &constantForce;
    
    // Création de l'effet
    IDirectInputEffect* pEffect = nullptr;
    HRESULT hr = m_pDevice->CreateEffect(GUID_ConstantForce, &eff, &pEffect, nullptr);
    
    if (SUCCEEDED(hr))
    {
        m_Effects[name] = pEffect;
        g_Logger.Info("  Effet constant créé: ", name, " (Force: ", force, ")");
        return true;
    }
    else
    {
        g_Logger.Error("  Erreur création effet constant ", name, ": ", Logger::Hex(hr));
        return false;
    }
}

bool ForceEffectSimulator::CreatePeriodicEffect(const std::string& name, const GUID& effectType, 
                                               DWORD magnitude, DWORD period, DWORD phase)
{
    DIEFFECT eff;
    ZeroMemory(&eff, sizeof(eff));
    
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE_DURATION; // Effets périodiques en continu
    eff.dwSamplePeriod = 0;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = 1;
    
    DWORD axes = DIJOFS_X;
    eff.rgdwAxes = &axes;
    
    LONG directions = 0;
    eff.rglDirection = &directions;
    
    // Paramètres périodiques
    DIPERIODIC periodicParams;
    ZeroMemory(&periodicParams, sizeof(periodicParams));
    periodicParams.dwMagnitude = magnitude;
    periodicParams.lOffset = 0;
    periodicParams.dwPhase = phase;
    periodicParams.dwPeriod = period * 1000; // Conversion en microsecondes
    
    eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams = &periodicParams;
    
    IDirectInputEffect* pEffect = nullptr;
    HRESULT hr = m_pDevice->CreateEffect(effectType, &eff, &pEffect, nullptr);
    
    if (SUCCEEDED(hr))
    {
        m_Effects[name] = pEffect;
        g_Logger.Info("  Effet périodique créé: ", name, 
                     " (Magnitude: ", magnitude, ", Période: ", period, "ms)");
        return true;
    }
    else
    {
        g_Logger.Error("  Erreur création effet périodique ", name, ": ", Logger::Hex(hr));
        return false;
    }
}

bool ForceEffectSimulator::CreateRampEffect(const std::string& name, LONG startForce, LONG endForce)
{
    DIEFFECT eff;
    ZeroMemory(&eff, sizeof(eff));
    
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = 3000; // 3 secondes pour mieux sentir la rampe
    eff.dwSamplePeriod = 0;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = 1;
    
    DWORD axes = DIJOFS_X;
    eff.rgdwAxes = &axes;
    
    LONG directions = startForce > 0 ? 1 : -1; // Direction basée sur le signe
    eff.rglDirection = &directions;
    
    // Paramètres rampe
    DIRAMPFORCE rampParams;
    rampParams.lStart = abs(startForce);
    rampParams.lEnd = abs(endForce);
    
    eff.cbTypeSpecificParams = sizeof(DIRAMPFORCE);
    eff.lpvTypeSpecificParams = &rampParams;
    
    IDirectInputEffect* pEffect = nullptr;
    HRESULT hr = m_pDevice->CreateEffect(GUID_RampForce, &eff, &pEffect, nullptr);
    
    if (SUCCEEDED(hr))
    {
        m_Effects[name] = pEffect;
        g_Logger.Info("  Effet rampe créé: ", name, " (", startForce, " -> ", endForce, ")");
        return true;
    }
    else
    {
        g_Logger.Error("  Erreur création effet rampe ", name, ": ", Logger::Hex(hr));
        return false;
    }
}

/**
 * Crée un effet de condition (ressort, amortissement, etc.).
 * @param name Nom de l'effet.
 * @param effectType Type GUID.
 * @param coefficient Coefficient de résistance.
 * @param saturation Saturation maximale.
 * @return true si succès.
 */
bool ForceEffectSimulator::CreateConditionEffect(const std::string& name, const GUID& effectType,
                                                LONG coefficient, LONG saturation)
{
    DIEFFECT eff;
    ZeroMemory(&eff, sizeof(eff));
    
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE_DURATION; // Conditions permanentes
    eff.dwSamplePeriod = 0;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = 1;
    
    DWORD axes = DIJOFS_X;
    eff.rgdwAxes = &axes;
    
    LONG directions = 0;
    eff.rglDirection = &directions;
    
    // Paramètres de condition - CRITIQUE pour les effets de condition
    DICONDITION conditionParams;
    ZeroMemory(&conditionParams, sizeof(conditionParams));
    conditionParams.lOffset = 0; // Pas de décalage
    conditionParams.lPositiveCoefficient = coefficient; // Force dans le sens positif
    conditionParams.lNegativeCoefficient = coefficient; // Force dans le sens négatif
    conditionParams.dwPositiveSaturation = (DWORD)saturation; // Saturation maximale
    conditionParams.dwNegativeSaturation = (DWORD)saturation; // Saturation maximale
    conditionParams.lDeadBand = 500; // Zone morte réduite pour plus de réactivité
    
    eff.cbTypeSpecificParams = sizeof(DICONDITION);
    eff.lpvTypeSpecificParams = &conditionParams;
    
    IDirectInputEffect* pEffect = nullptr;
    HRESULT hr = m_pDevice->CreateEffect(effectType, &eff, &pEffect, nullptr);
    
    if (SUCCEEDED(hr))
    {
        m_Effects[name] = pEffect;
        g_Logger.Info("  Effet condition créé: ", name, 
                     " (Coeff: ", coefficient, ", DeadBand: ", conditionParams.lDeadBand, ")");
        return true;
    }
    else
    {
        g_Logger.Error("  Erreur création effet condition ", name, ": ", Logger::Hex(hr));
        return false;
    }
}

/**
 * Boucle principale du simulateur : gère l'UI console, les entrées clavier et le thread de mise à jour.
 */
void ForceEffectSimulator::Run()
{
    if (m_Effects.empty())
    {
        std::cerr << "Aucun effet disponible" << std::endl;
        return;
    }
    
    m_bRunning = true;
    
    // Démarrage du thread de mise à jour
    m_UpdateThread = std::thread(&ForceEffectSimulator::UpdateLoop, this);
    
    DisplayHelp();
    DisplayStatus();
    
    // Boucle principale d'interface utilisateur
    while (m_bRunning)
    {
        if (_kbhit())
        {
            int key = _getch();
            
            switch (key)
            {
            case 27: // ESC
                if (m_bShowingHelp)
                {
                    m_bShowingHelp = false; // Sort du mode aide
                }
                else
                {
                    m_bRunning = false; // Quitte le programme
                }
                break;
                
            case ' ': // SPACE - Play/Stop
                if (!m_bShowingHelp)
                {
                    if (m_bEffectPlaying)
                        StopCurrentEffect();
                    else
                        PlayCurrentEffect();
                }
                break;
                
            case 's': // S - Stop all
            case 'S':
                if (!m_bShowingHelp)
                {
                    StopAllEffects();
                }
                break;
                
            case 'n': // N - Next effect
            case 'N':
                if (!m_bShowingHelp)
                {
                    NextEffect();
                }
                break;
                
            case 'p': // P - Previous effect
            case 'P':
                if (!m_bShowingHelp)
                {
                    PreviousEffect();
                }
                break;
                
            case '+': // Augmenter intensité
            case '=':
                if (!m_bShowingHelp)
                {
                    AdjustIntensity(500);
                }
                break;
                
            case '-': // Diminuer intensité
            case '_':
                if (!m_bShowingHelp)
                {
                    AdjustIntensity(-500);
                }
                break;
                
            case 224: // Touches spéciales
                {
                    int special = _getch();
                    if (!m_bShowingHelp)
                    {
                        switch (special)
                        {
                        case 75: // Flèche gauche - Direction
                            AdjustDirection(-1000);
                            break;
                        case 77: // Flèche droite - Direction
                            AdjustDirection(1000);
                            break;
                        case 72: // Flèche haut - Durée +
                            AdjustDuration(500);
                            break;
                        case 80: // Flèche bas - Durée -
                            AdjustDuration(-500);
                            break;
                        }
                    }
                }
                break;
                
            case 'h': // H - Help
            case 'H':
                m_bShowingHelp = !m_bShowingHelp; // Toggle mode aide
                break;
            }
            
            // Affichage conditionnel
            if (m_bShowingHelp)
            {
                DisplayHelp();
            }
            else
            {
                DisplayStatus();
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Arrêt propre
    StopAllEffects();
    
    if (m_UpdateThread.joinable())
    {
        m_UpdateThread.join();
    }
}

/**
 * Thread de mise à jour : actualise l'état du périphérique à intervalle régulier.
 */
void ForceEffectSimulator::UpdateLoop()
{
    while (m_bRunning)
    {
        UpdateDeviceState();
        std::this_thread::sleep_for(std::chrono::milliseconds(UPDATE_INTERVAL));
    }
}

/**
 * Met à jour l'état du périphérique (acquisition, polling, lecture des axes/boutons).
 */
void ForceEffectSimulator::UpdateDeviceState()
{
    if (!m_pDevice) return;
    
    // Tentative de réacquisition si perdue
    if (!m_bDeviceAcquired)
    {
        HRESULT hr = m_pDevice->Acquire();
        if (SUCCEEDED(hr))
        {
            m_bDeviceAcquired = true;
        }
        else
        {
            return; // Réessaiera au prochain cycle
        }
    }
    
    // Lecture de l'état du joystick
    HRESULT hr = m_pDevice->Poll();
    if (FAILED(hr))
    {
        hr = m_pDevice->Acquire();
        if (FAILED(hr)) 
        {
            m_bDeviceAcquired = false;
            return;
        }
    }
    
    hr = m_pDevice->GetDeviceState(sizeof(DIJOYSTATE2), &m_JoyState);
    if (FAILED(hr))
    {
        m_bDeviceAcquired = false;
    }
}

void ForceEffectSimulator::PlayCurrentEffect()
{
    if (m_EffectNames.empty()) return;
    
    StopAllEffects();
    
    const std::string& effectName = m_EffectNames[m_CurrentEffectIndex];
    auto it = m_Effects.find(effectName);
    
    if (it != m_Effects.end())
    {
        // Debug: Vérification de l'état de l'effet avant de le jouer
        DWORD status = 0;
        it->second->GetEffectStatus(&status);
        
        HRESULT hr = it->second->Start(1, 0); // 1 itération, 0 = pas de flags
        if (SUCCEEDED(hr))
        {
            m_bEffectPlaying = true;
            g_Logger.Success(">>> EFFET JOUÉ: " + effectName + " <<<");
            
            // Vérification post-lancement
            it->second->GetEffectStatus(&status);
            if (status & DIEGES_PLAYING)
            {
                g_Logger.Debug("    Status: EN COURS");
            }
            else
            {
                g_Logger.Warning("    Effet lancé mais status indique qu'il ne joue pas!");
            }
        }
        else
        {
            g_Logger.Error("Erreur lors de la lecture de l'effet: ", Logger::Hex(hr));
            
            // Tentative de diagnostic
            if (hr == DIERR_NOTEXCLUSIVEACQUIRED)
            {
                g_Logger.Error("  -> Device non acquis en mode exclusif");
            }
            else if (hr == DIERR_INPUTLOST)
            {
                g_Logger.Error("  -> Acquisition du device perdue");
            }
            else if (hr == DIERR_NOTDOWNLOADED)
            {
                g_Logger.Warning("  -> Effet non téléchargé sur le device");
                // Tentative de download
                hr = it->second->Download();
                if (SUCCEEDED(hr))
                {
                    g_Logger.Info("  -> Effet téléchargé, nouvel essai...");
                    hr = it->second->Start(1, 0);
                    if (SUCCEEDED(hr))
                    {
                        m_bEffectPlaying = true;
                        g_Logger.Success("  -> Succès!");
                    }
                }
            }
        }
    }
}

void ForceEffectSimulator::StopCurrentEffect()
{
    if (m_EffectNames.empty()) return;
    
    const std::string& effectName = m_EffectNames[m_CurrentEffectIndex];
    auto it = m_Effects.find(effectName);
    
    if (it != m_Effects.end())
    {
        it->second->Stop();
        m_bEffectPlaying = false;
        g_Logger.Info(">>> EFFET ARRÊTÉ <<<");
    }
}

void ForceEffectSimulator::StopAllEffects()
{
    for (auto& pair : m_Effects)
    {
        pair.second->Stop();
    }
    m_bEffectPlaying = false;
}

void ForceEffectSimulator::NextEffect()
{
    if (m_EffectNames.empty()) return;
    
    StopCurrentEffect();
    m_CurrentEffectIndex = (m_CurrentEffectIndex + 1) % m_EffectNames.size();
}

void ForceEffectSimulator::PreviousEffect()
{
    if (m_EffectNames.empty()) return;
    
    StopCurrentEffect();
    m_CurrentEffectIndex = (m_CurrentEffectIndex - 1 + m_EffectNames.size()) % m_EffectNames.size();
}

void ForceEffectSimulator::AdjustIntensity(int delta)
{
    m_ForceIntensity += delta;
    m_ForceIntensity = std::max(-MAX_FORCE, std::min(MAX_FORCE, m_ForceIntensity));
    
    // Mise à jour de l'effet courant si c'est un effet modifiable
    if (m_bEffectPlaying && !m_EffectNames.empty())
    {
        const std::string& effectName = m_EffectNames[m_CurrentEffectIndex];
        auto it = m_Effects.find(effectName);
        
        if (it != m_Effects.end())
        {
            // Pour les effets constants, on peut modifier la magnitude
            if (effectName.find("Constant") != std::string::npos)
            {
                DIEFFECT eff;
                ZeroMemory(&eff, sizeof(eff));
                eff.dwSize = sizeof(DIEFFECT);
                
                DICONSTANTFORCE constantForce;
                constantForce.lMagnitude = m_ForceIntensity;
                
                eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
                eff.lpvTypeSpecificParams = &constantForce;
                eff.dwFlags = DIEFF_OBJECTOFFSETS;
                
                it->second->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
            }
            // Pour les effets périodiques
            else if (effectName.find("Sinus") != std::string::npos || 
                     effectName.find("Carre") != std::string::npos ||
                     effectName.find("Triangle") != std::string::npos ||
                     effectName.find("Dent_Scie") != std::string::npos)
            {
                DIEFFECT eff;
                ZeroMemory(&eff, sizeof(eff));
                eff.dwSize = sizeof(DIEFFECT);
                
                DIPERIODIC periodicParams;
                ZeroMemory(&periodicParams, sizeof(periodicParams));
                periodicParams.dwMagnitude = abs(m_ForceIntensity);
                periodicParams.lOffset = 0;
                periodicParams.dwPhase = 0;
                periodicParams.dwPeriod = 200 * 1000; // 200ms par défaut
                
                eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
                eff.lpvTypeSpecificParams = &periodicParams;
                eff.dwFlags = DIEFF_OBJECTOFFSETS;
                
                it->second->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
            }
        }
    }
}

void ForceEffectSimulator::AdjustDirection(int delta)
{
    m_EffectDirection += delta;
    m_EffectDirection = std::max(-MAX_FORCE, std::min(MAX_FORCE, m_EffectDirection));
    
    // Note: La direction est plus complexe à modifier en temps réel avec DirectInput
    // Cela nécessiterait de recréer l'effet avec de nouveaux paramètres
}

void ForceEffectSimulator::AdjustDuration(int delta)
{
    if (m_EffectDuration == INFINITE_DURATION)
    {
        m_EffectDuration = 2000; // Passe de infini à 2 secondes
    }
    else
    {
        m_EffectDuration += delta;
        m_EffectDuration = std::max(100UL, std::min(10000UL, m_EffectDuration));
    }
}

void ForceEffectSimulator::DisplayStatus()
{
    system("cls"); // Clear screen sur Windows
    
    std::cout << "=== SIMULATEUR FORCE FEEDBACK SIDEWINDER ===" << std::endl;
    std::cout << "=============================================" << std::endl;
    
    // État du périphérique
    std::cout << "Device: " << (m_bDeviceAcquired ? "CONNECTÉ" : "DÉCONNECTÉ") << std::endl;
    
    if (m_bDeviceAcquired)
    {
        std::cout << "Position volant: " << m_JoyState.lX << std::endl;
        std::cout << "Pédales: Acc=" << m_JoyState.lY << " Frein=" << m_JoyState.lZ << std::endl;
        
        // Affichage des boutons pressés
        std::cout << "Boutons: ";
        bool hasButtons = false;
        for (int i = 0; i < 32; i++)
        {
            if (m_JoyState.rgbButtons[i] & 0x80)
            {
                std::cout << i << " ";
                hasButtons = true;
            }
        }
        if (!hasButtons) std::cout << "Aucun";
        std::cout << std::endl;
    }
    
    std::cout << "=============================================" << std::endl;
    
    // Effet courant
    if (!m_EffectNames.empty())
    {
        std::cout << "Effet courant: [" << (m_CurrentEffectIndex + 1) << "/" << m_EffectNames.size() << "] ";
        std::cout << m_EffectNames[m_CurrentEffectIndex];
        std::cout << " " << (m_bEffectPlaying ? "[EN COURS]" : "[ARRÊTÉ]") << std::endl;
    }
    
    // Paramètres
    std::cout << "Intensité: " << FormatForce(m_ForceIntensity) << std::endl;
    std::cout << "Direction: " << FormatDirection(m_EffectDirection) << std::endl;
    std::cout << "Durée: " << FormatDuration(m_EffectDuration) << std::endl;
    
    std::cout << "=============================================" << std::endl;
    
    // Liste des effets disponibles
    std::cout << "Effets disponibles:" << std::endl;
    for (size_t i = 0; i < m_EffectNames.size(); i++)
    {
        std::cout << "  " << (i == m_CurrentEffectIndex ? "►" : " ") << " " << m_EffectNames[i] << std::endl;
    }
    
    std::cout << "=============================================" << std::endl;
}

void ForceEffectSimulator::DisplayHelp()
{
    system("cls"); // Clear screen sur Windows
    
    std::cout << "===============================================" << std::endl;
    std::cout << "           AIDE - SIMULATEUR FFB              " << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << std::endl;
    
    std::cout << "CONTRÔLES PRINCIPAUX:" << std::endl;
    std::cout << "  ESPACE      Jouer/Arrêter l'effet courant" << std::endl;
    std::cout << "  N           Effet suivant" << std::endl;
    std::cout << "  P           Effet précédent" << std::endl;
    std::cout << "  S           Arrêter tous les effets" << std::endl;
    std::cout << std::endl;
    
    std::cout << "AJUSTEMENTS:" << std::endl;
    std::cout << "  +  =        Augmenter l'intensité (+500)" << std::endl;
    std::cout << "  -  _        Diminuer l'intensité (-500)" << std::endl;
    std::cout << "  ← →         Ajuster la direction (±1000)" << std::endl;
    std::cout << "  ↑ ↓         Ajuster la durée (±500ms)" << std::endl;
    std::cout << std::endl;
    
    std::cout << "NAVIGATION:" << std::endl;
    std::cout << "  H           Basculer aide ON/OFF" << std::endl;
    std::cout << "  ESC         Quitter l'aide ou le programme" << std::endl;
    std::cout << std::endl;
    
    std::cout << "EFFETS DISPONIBLES:" << std::endl;
    std::cout << "  • Effets constants (résistance directionnelle)" << std::endl;
    std::cout << "  • Effets périodiques (vibrations rythmées)" << std::endl;
    std::cout << "  • Effets rampe (force progressive)" << std::endl;
    std::cout << "  • Effets condition (ressort, amortissement)" << std::endl;
    std::cout << std::endl;
    
    std::cout << "CONSEILS D'UTILISATION:" << std::endl;
    std::cout << "  1. Commencez par 'Constant_Droite' ou 'Sinus'" << std::endl;
    std::cout << "  2. Ajustez l'intensité selon votre confort" << std::endl;
    std::cout << "  3. Les effets 'Condition' simulent des résistances" << std::endl;
    std::cout << "  4. Utilisez 'S' pour arrêter rapidement si nécessaire" << std::endl;
    std::cout << std::endl;
    
    std::cout << "===============================================" << std::endl;
    std::cout << "   Appuyez sur H ou ESC pour revenir au menu  " << std::endl;
    std::cout << "===============================================" << std::endl;
}

std::string ForceEffectSimulator::FormatForce(LONG force)
{
    double percentage = (force * 100.0) / MAX_FORCE;
    char buffer[64];
    sprintf_s(buffer, sizeof(buffer), "%d (%.1f%%)", (int)force, percentage);
    return std::string(buffer);
}

std::string ForceEffectSimulator::FormatDirection(LONG direction)
{
    if (direction == 0) return "Centre";
    else if (direction > 0) return "Droite (" + std::to_string(direction) + ")";
    else return "Gauche (" + std::to_string(direction) + ")";
}

std::string ForceEffectSimulator::FormatDuration(DWORD duration)
{
    if (duration == INFINITE_DURATION) return "Infinie";
    else return std::to_string(duration) + "ms";
}

void ForceEffectSimulator::CleanupEffects()
{
    for (auto& pair : m_Effects)
    {
        if (pair.second)
        {
            pair.second->Stop();
            pair.second->Release();
        }
    }
    m_Effects.clear();
    m_EffectNames.clear();
}

void ForceEffectSimulator::Shutdown()
{
    m_bRunning = false;
    
    if (m_UpdateThread.joinable())
    {
        m_UpdateThread.join();
    }
    
    StopAllEffects();
    CleanupEffects();
    
    if (m_pDevice)
    {
        m_pDevice->Unacquire();
        m_pDevice->Release();
        m_pDevice = nullptr;
    }
    
    if (m_pDI)
    {
        m_pDI->Release();
        m_pDI = nullptr;
    }
}


//==============================================================================
// Fonctions utiles
//==============================================================================

/**
 * @brief Get the process name
 * 
 * @return std::string the current process name
 */
using TSTRING = std::basic_string<TCHAR>;

TSTRING GetProcessName()
{
    // Get program file name
    TCHAR buffer[MAX_PATH]={0};
    TCHAR * progName;
    DWORD bufSize=sizeof(buffer)/sizeof(*buffer);
    // Get the fully-qualified path of the executable
    if(GetModuleFileName(NULL, buffer, bufSize)==bufSize)
    {
        std::cerr << "Erreur d'allocation buffer" << std::endl;
    }
    // now buffer = "c:\whatever\yourexecutable.exe"

    // Go to the beginning of the file name
    progName = PathFindFileName(buffer);
    // now out = "yourexecutable.exe"

    // Set the dot before the extension to 0 (terminate the string there)
    *(PathFindExtension(progName)) = 0;
    // now out = "yourexecutable"
    return progName;
}
//==============================================================================
// FONCTION MAIN
//==============================================================================

int main()
{
    // Configuration de la console pour l'UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // Génération du nom de fichier log avec timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &time_t_now);
    
    char logDate[32];
    strftime(logDate, sizeof(logDate), "%Y%m%d_%H%M%S", &timeinfo);
    
    char logFilename[256];
    sprintf(logFilename, "%s_%s.log", GetProcessName().c_str(), logDate);
    // Ouverture du fichier log
    if (!g_Logger.Open(logFilename))
    {
        std::cerr << "ATTENTION: Impossible de créer le fichier log: " << logFilename << std::endl;
        std::cerr << "Les logs seront affichés uniquement dans la console." << std::endl;
    }
    else
    {
        std::cout << "Fichier log créé: " << logFilename << std::endl;
    }
    
    g_Logger.Info("Démarrage du simulateur Force Feedback...");
    
    ForceEffectSimulator simulator;
    
    if (!simulator.Initialize())
    {
        g_Logger.Error("Échec de l'initialisation!");
        std::cout << "\nAppuyez sur une touche pour continuer..." << std::endl;
        _getch();
        g_Logger.Close();
        return -1;
    }
    
    simulator.Run();
    
    g_Logger.Info("Arrêt du simulateur...");
    simulator.Shutdown();
    
    g_Logger.Info("Fichier log sauvegardé: " + g_Logger.GetFilename());
    g_Logger.Close();
    
    return 0;
}

//==============================================================================
// MAKEFILE SUGGÉRÉ (Commentaire)
//==============================================================================

/*
Pour compiler avec Visual Studio ou MinGW:

# Visual Studio
cl /EHsc FFB_Simulator.cpp dinput8.lib dxguid.lib

# MinGW
g++ -std=c++11 FFB_Simulator.cpp -ldinput8 -ldxguid -o FFB_Simulator.exe

Dépendances requises:
- DirectX SDK ou Windows SDK
- Bibliothèques: dinput8.lib, dxguid.lib
- Headers: dinput.h

Configuration requise:
- Windows 7 ou supérieur
- Microsoft Sidewinder Force Feedback Wheel connecté
- Pilotes DirectInput installés
*/