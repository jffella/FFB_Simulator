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
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <chrono>
#include <conio.h>
#include <memory>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

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
    // Indique si un effet est en cours de lecture
    bool m_bEffectPlaying;

    // Thread dédié à la mise à jour de l'état du périphérique
    std::thread m_UpdateThread;
    // Indique si la boucle principale est active
    bool m_bRunning;

    // Intensité actuelle de la force appliquée
    LONG m_ForceIntensity;
    // Durée de l'effet courant (en ms)
    DWORD m_EffectDuration;
    // Direction de l'effet courant
    LONG m_EffectDirection;

    public:
        /**
         * Constructeur : initialise les membres et prépare le simulateur.
         */
        ForceEffectSimulator();
        /**
         * Destructeur : libère les ressources et arrête le simulateur.
         */
        ~ForceEffectSimulator();

        /**
         * Initialise le simulateur (DirectInput, périphérique, effets).
         * @return true si succès.
         */
        bool Initialize();
        /**
         * Arrête et nettoie le simulateur.
         */
        void Shutdown();
        /**
         * Lance la boucle principale et l'interface utilisateur.
         */
        void Run();

    private:
        // Initialise DirectInput
        bool InitializeDirectInput();
        // Trouve et initialise le périphérique Sidewinder
        bool FindAndInitDevice();
        // Définit le niveau coopératif du périphérique
        bool SetupCooperativeLevel(HWND hwnd);
        // Configure le format de données du périphérique
        bool SetupDataFormat();
        // Configure les capacités force feedback
        bool SetupForceFeedback();

        // Crée tous les effets supportés
        bool CreateAllEffects();
        // Crée un effet constant
        bool CreateConstantEffect(const std::string& name, LONG force, LONG direction = 0);
        // Crée un effet périodique
        bool CreatePeriodicEffect(const std::string& name, const GUID& effectType, 
                                DWORD magnitude, DWORD period, DWORD phase = 0);
        // Crée un effet rampe
        bool CreateRampEffect(const std::string& name, LONG startForce, LONG endForce);
        // Crée un effet de condition
        bool CreateConditionEffect(const std::string& name, const GUID& effectType,
                                 LONG coefficient, LONG saturation);

        // Joue l'effet courant
        void PlayCurrentEffect();
        // Arrête l'effet courant
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

/**
 * Constructeur : Initialise les membres et l'état du simulateur.
 */
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

/**
 * Destructeur : Libère les ressources et arrête le simulateur proprement.
 */
ForceEffectSimulator::~ForceEffectSimulator()
{
    Shutdown();
}

/**
 * Initialise DirectInput, détecte le périphérique Sidewinder, crée les effets et prépare le simulateur.
 * @return true si l'initialisation est réussie, false sinon.
 */
bool ForceEffectSimulator::Initialize()
{
    std::cout << "=== Simulateur Force Feedback DirectInput ===" << std::endl;
    std::cout << "Initialisation..." << std::endl;
    
    if (!InitializeDirectInput())
    {
        std::cerr << "Erreur: Impossible d'initialiser DirectInput" << std::endl;
        return false;
    }
    
    if (!FindAndInitDevice())
    {
        std::cerr << "Erreur: Impossible de trouver le volant Sidewinder" << std::endl;
        return false;
    }
    
    if (!CreateAllEffects())
    {
        std::cerr << "Erreur: Impossible de créer les effets force feedback" << std::endl;
        return false;
    }
    
    std::cout << "Initialisation terminée avec succès!" << std::endl;
    std::cout << "Effets disponibles: " << m_Effects.size() << std::endl;
    
    return true;
}

/**
 * Initialise l'interface DirectInput principale.
 * @return true si succès, false sinon.
 */
bool ForceEffectSimulator::InitializeDirectInput()
{
    HRESULT hr = DirectInput8Create(GetModuleHandle(nullptr),
                                   DIRECTINPUT_VERSION,
                                   IID_IDirectInput8,
                                   (void**)&m_pDI,
                                   nullptr);
    
    if (FAILED(hr))
    {
        std::cerr << "DirectInput8Create failed: " << std::hex << hr << std::endl;
        return false;
    }
    
    return true;
}

/**
 * Callback d'énumération des joysticks : sélectionne le Sidewinder via VID/PID.
 */
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
            
            std::cout << "Device trouvé: " << pdidInstance->tszProductName 
                      << " (VID: 0x" << std::hex << vid << ", PID: 0x" << pid << ")" << std::dec << std::endl;
            
            if (vid == SIDEWINDER_VID && pid == SIDEWINDER_PID)
            {
                std::cout << "Microsoft Sidewinder Force Feedback Wheel détecté!" << std::endl;
                
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

/**
 * Trouve et initialise le périphérique Sidewinder, configure le format, le niveau coopératif et le force feedback.
 * @return true si le périphérique est prêt, false sinon.
 */
bool ForceEffectSimulator::FindAndInitDevice()
{
    // Énumération des joysticks force feedback
    HRESULT hr = m_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL,
                                   EnumJoysticksCallback,
                                   this,
                                   DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    
    if (FAILED(hr) || !m_pDevice)
    {
        std::cerr << "Aucun volant Sidewinder trouvé ou erreur d'énumération" << std::endl;
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
        std::cout << "Device acquis avec succès" << std::endl;
    }
    else
    {
        std::cout << "Attention: Device non acquis (sera tenté plus tard)" << std::endl;
    }
    
    return true;
}

/**
 * Configure le format de données du périphérique pour DirectInput.
 * @return true si succès, false sinon.
 */
bool ForceEffectSimulator::SetupDataFormat()
{
    HRESULT hr = m_pDevice->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr))
    {
        std::cerr << "SetDataFormat failed: " << std::hex << hr << std::endl;
        return false;
    }
    return true;
}

/**
 * Définit le niveau coopératif du périphérique (exclusif/background).
 * @param hwnd Fenêtre cible.
 * @return true si succès, false sinon.
 */
bool ForceEffectSimulator::SetupCooperativeLevel(HWND hwnd)
{
    // DISCL_EXCLUSIVE nécessaire pour force feedback
    // DISCL_BACKGROUND pour permettre l'utilisation même si la fenêtre n'a pas le focus
    HRESULT hr = m_pDevice->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    if (FAILED(hr))
    {
        std::cerr << "SetCooperativeLevel failed: " << std::hex << hr << std::endl;
        return false;
    }
    return true;
}

/**
 * Callback d'énumération des objets du périphérique : configure les axes pour le force feedback.
 */
BOOL CALLBACK ForceEffectSimulator::EnumObjectsCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext)
{
    ForceEffectSimulator* pThis = static_cast<ForceEffectSimulator*>(pContext);
    
    std::cout << "Objet: " << pdidoi->tszName << " (Type: 0x" << std::hex << pdidoi->dwType << ")" << std::dec;
    
    // Configuration des axes force feedback
    if (pdidoi->dwFlags & DIDOI_FFACTUATOR)
    {
        std::cout << " [Force Feedback]";
        
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
            std::cout << " (Erreur config range)";
        }
    }
    
    std::cout << std::endl;
    return DIENUM_CONTINUE;
}

/**
 * Configure les capacités force feedback du périphérique (autocenter, gain).
 * @return true si succès, false sinon.
 */
bool ForceEffectSimulator::SetupForceFeedback()
{
    // Vérification des capacités force feedback
    DIDEVCAPS caps;
    caps.dwSize = sizeof(DIDEVCAPS);
    
    HRESULT hr = m_pDevice->GetCapabilities(&caps);
    if (FAILED(hr))
    {
        std::cerr << "GetCapabilities failed" << std::endl;
        return false;
    }
    
    std::cout << "Capacités Force Feedback:" << std::endl;
    std::cout << "  Axes FF: " << caps.dwAxes << std::endl;
    std::cout << "  Effets simultanés: " << caps.dwFFDriverVersion << std::endl;
    
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
        std::cout << "Attention: Impossible d'activer l'autocenter" << std::endl;
    }
    
    // Configuration du gain général
    dipProp.dwData = DI_FFNOMINALMAX; // Gain maximum
    hr = m_pDevice->SetProperty(DIPROP_FFGAIN, &dipProp.diph);
    if (FAILED(hr))
    {
        std::cout << "Attention: Impossible de configurer le gain" << std::endl;
    }
    
    return true;
}

/**
 * Crée tous les effets de force feedback supportés et les ajoute à la map d'effets.
 * @return true si au moins un effet est créé.
 */
bool ForceEffectSimulator::CreateAllEffects()
{
    std::cout << "Création des effets..." << std::endl;
    
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
    
    std::cout << "Effets créés: " << m_Effects.size() << std::endl;
    
    // Construction de la liste des noms pour navigation
    for (const auto& pair : m_Effects)
    {
        m_EffectNames.push_back(pair.first);
    }
    
    return success && !m_Effects.empty();
}

/**
 * Crée un effet constant (force continue) avec nom, force et direction.
 * @param name Nom de l'effet.
 * @param force Intensité de la force.
 * @param direction Direction (cartésienne).
 * @return true si succès.
 */
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
        std::cout << "  Effet constant créé: " << name << " (Force: " << force << ")" << std::endl;
        return true;
    }
    else
    {
        std::cerr << "  Erreur création effet constant " << name << ": " << std::hex << hr << std::endl;
        return false;
    }
}

/**
 * Crée un effet périodique (sinus, carré, etc.) avec magnitude, période et phase.
 * @param name Nom de l'effet.
 * @param effectType Type GUID de l'effet.
 * @param magnitude Intensité.
 * @param period Période.
 * @param phase Phase initiale.
 * @return true si succès.
 */
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
        std::cout << "  Effet périodique créé: " << name << " (Magnitude: " << magnitude << ", Période: " << period << "ms)" << std::endl;
        return true;
    }
    else
    {
        std::cerr << "  Erreur création effet périodique " << name << ": " << std::hex << hr << std::endl;
        return false;
    }
}

/**
 * Crée un effet rampe (force croissante/décroissante).
 * @param name Nom de l'effet.
 * @param startForce Force initiale.
 * @param endForce Force finale.
 * @return true si succès.
 */
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
        std::cout << "  Effet rampe créé: " << name << " (" << startForce << " -> " << endForce << ")" << std::endl;
        return true;
    }
    else
    {
        std::cerr << "  Erreur création effet rampe " << name << ": " << std::hex << hr << std::endl;
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
        std::cout << "  Effet condition créé: " << name << " (Coeff: " << coefficient << ", DeadBand: " << conditionParams.lDeadBand << ")" << std::endl;
        return true;
    }
    else
    {
        std::cerr << "  Erreur création effet condition " << name << ": " << std::hex << hr << std::endl;
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

/**
 * Joue l'effet courant sélectionné, gère les erreurs et diagnostics.
 */
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
            std::cout << "\n>>> EFFET JOUÉ: " << effectName << " <<<" << std::endl;
            
            // Vérification post-lancement
            it->second->GetEffectStatus(&status);
            if (status & DIEGES_PLAYING)
            {
                std::cout << "    Status: EN COURS" << std::endl;
            }
            else
            {
                std::cout << "    ATTENTION: Effet lancé mais status indique qu'il ne joue pas!" << std::endl;
            }
        }
        else
        {
            std::cerr << "Erreur lors de la lecture de l'effet: 0x" << std::hex << hr << std::dec << std::endl;
            
            // Tentative de diagnostic
            if (hr == DIERR_NOTEXCLUSIVEACQUIRED)
            {
                std::cerr << "  -> Device non acquis en mode exclusif" << std::endl;
            }
            else if (hr == DIERR_INPUTLOST)
            {
                std::cerr << "  -> Acquisition du device perdue" << std::endl;
            }
            else if (hr == DIERR_NOTDOWNLOADED)
            {
                std::cerr << "  -> Effet non téléchargé sur le device" << std::endl;
                // Tentative de download
                hr = it->second->Download();
                if (SUCCEEDED(hr))
                {
                    std::cout << "  -> Effet téléchargé, nouvel essai..." << std::endl;
                    hr = it->second->Start(1, 0);
                    if (SUCCEEDED(hr))
                    {
                        m_bEffectPlaying = true;
                        std::cout << "  -> Succès!" << std::endl;
                    }
                }
            }
        }
    }
}

/**
 * Arrête l'effet courant sélectionné.
 */
void ForceEffectSimulator::StopCurrentEffect()
{
    if (m_EffectNames.empty()) return;
    
    const std::string& effectName = m_EffectNames[m_CurrentEffectIndex];
    auto it = m_Effects.find(effectName);
    
    if (it != m_Effects.end())
    {
        it->second->Stop();
        m_bEffectPlaying = false;
        std::cout << "\n>>> EFFET ARRÊTÉ <<<" << std::endl;
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
// FONCTION MAIN
//==============================================================================

int main()
{
    // Configuration de la console pour l'UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    std::cout << "Démarrage du simulateur Force Feedback..." << std::endl;
    
    ForceEffectSimulator simulator;
    
    if (!simulator.Initialize())
    {
        std::cerr << "Échec de l'initialisation!" << std::endl;
        std::cout << "Appuyez sur une touche pour continuer..." << std::endl;
        _getch();
        return -1;
    }
    
    simulator.Run();
    
    std::cout << "Arrêt du simulateur..." << std::endl;
    simulator.Shutdown();
    
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