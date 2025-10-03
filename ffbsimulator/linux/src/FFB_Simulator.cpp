//==============================================================================
// FFB_Simulator_Linux.cpp - Simulateur d'effets à retour de force pour Linux
// Compatible Microsoft Sidewinder Force Feedback Wheel
// Copyright (c) 2024
//==============================================================================

#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <chrono>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <algorithm>

// Linux-specific headers
#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <termios.h>

//==============================================================================
// CONSTANTES
//==============================================================================

// IDs Microsoft Sidewinder Force Feedback Wheel
const uint16_t SIDEWINDER_VID = 0x045E;
const uint16_t SIDEWINDER_PID = 0x0034;

// Paramètres de force
const int16_t MAX_FORCE = 32767;         // Force maximale Linux FF
const uint32_t EFFECT_DURATION = 2000;   // Durée par défaut (ms)
const uint32_t INFINITE_DURATION = 0;    // 0 = infini sous Linux

// Refresh rate
const uint32_t UPDATE_INTERVAL = 16;     // ~60 FPS

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
        localtime_r(&time_t_now, &timeinfo);
        
        std::ostringstream oss;
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        oss << buf << '.' << std::setfill('0') << std::setw(3) << ms.count();
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
    
    /**
     * Méthode template principale pour logger avec paramètres variadiques.
     */
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
    
    /**
     * Helper pour formater en hexadécimal.
     */
    struct Hex
    {
        int value;
        explicit Hex(int v) : value(v) {}
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
// UTILITAIRES TERMINAL
//==============================================================================

/**
 * Désactive le mode canonique et l'echo du terminal pour _kbhit() style.
 */
class TerminalMode
{
private:
    struct termios m_OldSettings;
    bool m_Modified;
    
public:
    TerminalMode() : m_Modified(false) {}
    
    void SetRaw()
    {
        struct termios newSettings;
        tcgetattr(STDIN_FILENO, &m_OldSettings);
        newSettings = m_OldSettings;
        newSettings.c_lflag &= ~(ICANON | ECHO);
        newSettings.c_cc[VMIN] = 0;
        newSettings.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);
        m_Modified = true;
    }
    
    ~TerminalMode()
    {
        if (m_Modified)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &m_OldSettings);
        }
    }
};

/**
 * Équivalent de _kbhit() pour Linux.
 */
int kbhit()
{
    struct timeval tv = {0, 0};
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    return select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv) > 0;
}

//==============================================================================
// CLASSE PRINCIPALE
//==============================================================================

class ForceEffectSimulator
{
private:
    // État du périphérique
    int m_DeviceFd;              // File descriptor du device event
    int m_JoystickFd;            // File descriptor pour lire les axes/boutons
    std::string m_DevicePath;
    bool m_bDeviceOpen;
    
    // Mode d'affichage
    bool m_bShowingHelp;
    
    // Gestion des effets
    std::map<std::string, struct ff_effect> m_Effects;
    std::vector<std::string> m_EffectNames;
    int m_CurrentEffectIndex;
    bool m_bEffectPlaying;
    
    // Thread de mise à jour
    std::thread m_UpdateThread;
    bool m_bRunning;
    
    // Paramètres d'effet ajustables
    int16_t m_ForceIntensity;
    uint32_t m_EffectDuration;
    int16_t m_EffectDirection;
    
    // État des contrôles
    int16_t m_SteeringValue;
    int16_t m_Pedal1Value;
    int16_t m_Pedal2Value;
    uint32_t m_ButtonState;
    
    // Terminal mode
    TerminalMode m_TerminalMode;
    
public:
    ForceEffectSimulator();
    ~ForceEffectSimulator();
    
    /**
     * Initialise le simulateur (device, effets).
     * @return true si succès.
     */
    bool Initialize();
    void Shutdown();
    void Run();
    
private:
    // Initialisation
    bool FindDevice();
    bool OpenDevice();
    bool SetupForceFeedback();
    
    // Gestion des effets
    bool CreateAllEffects();
    bool CreateConstantEffect(const std::string& name, int16_t force);
    bool CreatePeriodicEffect(const std::string& name, uint16_t waveform, 
                            uint16_t magnitude, uint16_t period);
    bool CreateRampEffect(const std::string& name, int16_t startForce, int16_t endForce);
    bool CreateConditionEffect(const std::string& name, uint16_t type,
                             int16_t coefficient, uint16_t saturation);
    
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
    static std::string FormatForce(int16_t force);
    static std::string FormatDirection(int16_t direction);
    static std::string FormatDuration(uint32_t duration);
    void CleanupEffects();
};

//==============================================================================
// IMPLÉMENTATION
//==============================================================================

ForceEffectSimulator::ForceEffectSimulator()
    : m_DeviceFd(-1)
    , m_JoystickFd(-1)
    , m_bDeviceOpen(false)
    , m_bShowingHelp(false)
    , m_CurrentEffectIndex(0)
    , m_bEffectPlaying(false)
    , m_bRunning(false)
    , m_ForceIntensity(16000)
    , m_EffectDuration(EFFECT_DURATION)
    , m_EffectDirection(0)
    , m_SteeringValue(0)
    , m_Pedal1Value(0)
    , m_Pedal2Value(0)
    , m_ButtonState(0)
{
}

ForceEffectSimulator::~ForceEffectSimulator()
{
    Shutdown();
}

bool ForceEffectSimulator::Initialize()
{
    g_Logger.Info("=== Simulateur Force Feedback Linux evdev ===");
    g_Logger.Info("Initialisation...");
    
    if (!FindDevice())
    {
        g_Logger.Error("Impossible de trouver le volant Sidewinder");
        return false;
    }
    
    if (!OpenDevice())
    {
        g_Logger.Error("Impossible d'ouvrir le périphérique");
        return false;
    }
    
    if (!SetupForceFeedback())
    {
        g_Logger.Error("Le force feedback n'est pas disponible");
        return false;
    }
    
    if (!CreateAllEffects())
    {
        g_Logger.Error("Impossible de créer les effets force feedback");
        return false;
    }
    
    g_Logger.Success("Initialisation terminée avec succès!");
    g_Logger.Info("Effets disponibles: ", m_Effects.size());
    
    return true;
}

/**
 * Recherche le device event correspondant au Sidewinder.
 * @return true si trouvé.
 */
bool ForceEffectSimulator::FindDevice()
{
    DIR* dir = opendir("/dev/input");
    if (!dir)
    {
        g_Logger.Error("Impossible d'ouvrir /dev/input");
        return false;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;
        
        std::string path = std::string("/dev/input/") + entry->d_name;
        int fd = open(path.c_str(), O_RDWR);
        if (fd < 0)
            continue;
        
        // Vérification du VID/PID
        struct input_id device_id;
        if (ioctl(fd, EVIOCGID, &device_id) >= 0)
        {
            if (device_id.vendor == SIDEWINDER_VID && device_id.product == SIDEWINDER_PID)
            {
                // Vérification des capacités FF
                unsigned long features[4];
                if (ioctl(fd, EVIOCGBIT(EV_FF, FF_MAX), features) >= 0)
                {
                    if (features[FF_CONSTANT/8] & (1 << (FF_CONSTANT % 8)))
                    {
                        m_DevicePath = path;
                        close(fd);
                        closedir(dir);
                        
                        g_Logger.Success("Microsoft Sidewinder Force Feedback Wheel détecté!");
                        g_Logger.Info("Device: ", path);
                        return true;
                    }
                }
            }
        }
        
        close(fd);
    }
    
    closedir(dir);
    g_Logger.Error("Aucun volant Sidewinder trouvé avec support FF");
    return false;
}

/**
 * Ouvre le device pour lecture et force feedback.
 */
bool ForceEffectSimulator::OpenDevice()
{
    m_DeviceFd = open(m_DevicePath.c_str(), O_RDWR);
    if (m_DeviceFd < 0)
    {
        g_Logger.Error("Impossible d'ouvrir ", m_DevicePath, " (permissions?)");
        g_Logger.Info("Essayez: sudo chmod 666 ", m_DevicePath);
        return false;
    }
    
    // Ouvrir aussi en lecture pour les axes/boutons
    m_JoystickFd = open(m_DevicePath.c_str(), O_RDONLY | O_NONBLOCK);
    
    // Récupération du nom du device
    char name[256] = "Unknown";
    ioctl(m_DeviceFd, EVIOCGNAME(sizeof(name)), name);
    g_Logger.Info("Device name: ", name);
    
    m_bDeviceOpen = true;
    return true;
}

/**
 * Vérifie et configure les capacités force feedback.
 */
bool ForceEffectSimulator::SetupForceFeedback()
{
    // Vérification du nombre d'effets simultanés
    int n_effects = 0;
    if (ioctl(m_DeviceFd, EVIOCGEFFECTS, &n_effects) < 0)
    {
        g_Logger.Error("EVIOCGEFFECTS failed");
        return false;
    }
    
    g_Logger.Info("Effets FF simultanés supportés: ", n_effects);
    
    // Vérification des types d'effets supportés
    unsigned long features[4];
    ioctl(m_DeviceFd, EVIOCGBIT(EV_FF, FF_MAX), features);
    
    g_Logger.Debug("Types d'effets supportés:");
    if (features[FF_CONSTANT/8] & (1 << (FF_CONSTANT % 8)))
        g_Logger.Debug("  - FF_CONSTANT");
    if (features[FF_PERIODIC/8] & (1 << (FF_PERIODIC % 8)))
        g_Logger.Debug("  - FF_PERIODIC");
    if (features[FF_RAMP/8] & (1 << (FF_RAMP % 8)))
        g_Logger.Debug("  - FF_RAMP");
    if (features[FF_SPRING/8] & (1 << (FF_SPRING % 8)))
        g_Logger.Debug("  - FF_SPRING");
    if (features[FF_DAMPER/8] & (1 << (FF_DAMPER % 8)))
        g_Logger.Debug("  - FF_DAMPER");
    
    // Désactivation de l'autocenter pour avoir le contrôle total
    struct input_event ie;
    ie.type = EV_FF;
    ie.code = FF_AUTOCENTER;
    ie.value = 0;
    if (write(m_DeviceFd, &ie, sizeof(ie)) != sizeof(ie))
    {
        g_Logger.Warning("Impossible de désactiver l'autocenter");
    }
    else
    {
        g_Logger.Info("Autocenter désactivé");
    }
    
    return true;
}

bool ForceEffectSimulator::CreateAllEffects()
{
    g_Logger.Info("Création des effets...");
    
    bool success = true;
    
    // Effets constants
    success &= CreateConstantEffect("Constant_Droite", 24000);
    success &= CreateConstantEffect("Constant_Gauche", -24000);
    success &= CreateConstantEffect("Constant_Fort", 32000);
    success &= CreateConstantEffect("Constant_Faible", 12000);
    
    // Effets périodiques
    success &= CreatePeriodicEffect("Sinus", FF_SINE, 20000, 200);
    success &= CreatePeriodicEffect("Carre", FF_SQUARE, 22000, 150);
    success &= CreatePeriodicEffect("Triangle", FF_TRIANGLE, 18000, 300);
    success &= CreatePeriodicEffect("Dent_Scie", FF_SAW_UP, 20000, 180);
    
    // Effets rampe
    success &= CreateRampEffect("Rampe_Montante", 5000, 30000);
    success &= CreateRampEffect("Rampe_Descendante", 30000, 5000);
    
    // Effets de condition
    success &= CreateConditionEffect("Ressort", FF_SPRING, 24000, 32767);
    success &= CreateConditionEffect("Amortissement", FF_DAMPER, 20000, 32767);
    success &= CreateConditionEffect("Inertie", FF_INERTIA, 18000, 32767);
    success &= CreateConditionEffect("Friction", FF_FRICTION, 15000, 32767);
    
    g_Logger.Info("Effets créés: ", m_Effects.size());
    
    // Construction de la liste des noms pour navigation
    for (const auto& pair : m_Effects)
    {
        m_EffectNames.push_back(pair.first);
    }
    
    return success && !m_Effects.empty();
}

/**
 * Crée un effet constant (force directionnelle).
 */
bool ForceEffectSimulator::CreateConstantEffect(const std::string& name, int16_t force)
{
    struct ff_effect effect;
    memset(&effect, 0, sizeof(effect));
    
    effect.type = FF_CONSTANT;
    effect.id = -1; // Le kernel assignera un ID
    effect.u.constant.level = force;
    effect.direction = 0x4000; // 0 degrés (vers la droite pour valeur positive)
    effect.u.constant.envelope.attack_length = 0;
    effect.u.constant.envelope.attack_level = 0;
    effect.u.constant.envelope.fade_length = 0;
    effect.u.constant.envelope.fade_level = 0;
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = INFINITE_DURATION; // Infini
    effect.replay.delay = 0;
    
    if (ioctl(m_DeviceFd, EVIOCSFF, &effect) < 0)
    {
        g_Logger.Error("  Erreur création effet constant ", name, ": ", strerror(errno));
        return false;
    }
    
    m_Effects[name] = effect;
    g_Logger.Info("  Effet constant créé: ", name, " (Force: ", force, ", ID: ", effect.id, ")");
    return true;
}

/**
 * Crée un effet périodique (vibrations).
 */
bool ForceEffectSimulator::CreatePeriodicEffect(const std::string& name, uint16_t waveform,
                                               uint16_t magnitude, uint16_t period)
{
    struct ff_effect effect;
    memset(&effect, 0, sizeof(effect));
    
    effect.type = FF_PERIODIC;
    effect.id = -1;
    effect.u.periodic.waveform = waveform;
    effect.u.periodic.period = period;
    effect.u.periodic.magnitude = magnitude;
    effect.u.periodic.offset = 0;
    effect.u.periodic.phase = 0;
    effect.direction = 0x4000;
    effect.u.periodic.envelope.attack_length = 0;
    effect.u.periodic.envelope.attack_level = 0;
    effect.u.periodic.envelope.fade_length = 0;
    effect.u.periodic.envelope.fade_level = 0;
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = INFINITE_DURATION;
    effect.replay.delay = 0;
    
    if (ioctl(m_DeviceFd, EVIOCSFF, &effect) < 0)
    {
        g_Logger.Error("  Erreur création effet périodique ", name, ": ", strerror(errno));
        return false;
    }
    
    m_Effects[name] = effect;
    g_Logger.Info("  Effet périodique créé: ", name, " (Magnitude: ", magnitude, ", Période: ", period, "ms, ID: ", effect.id, ")");
    return true;
}

/**
 * Crée un effet rampe (force progressive).
 */
bool ForceEffectSimulator::CreateRampEffect(const std::string& name, int16_t startForce, int16_t endForce)
{
    struct ff_effect effect;
    memset(&effect, 0, sizeof(effect));
    
    effect.type = FF_RAMP;
    effect.id = -1;
    effect.u.ramp.start_level = startForce;
    effect.u.ramp.end_level = endForce;
    effect.direction = 0x4000;
    effect.u.ramp.envelope.attack_length = 0;
    effect.u.ramp.envelope.attack_level = 0;
    effect.u.ramp.envelope.fade_length = 0;
    effect.u.ramp.envelope.fade_level = 0;
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = 3000; // 3 secondes
    effect.replay.delay = 0;
    
    if (ioctl(m_DeviceFd, EVIOCSFF, &effect) < 0)
    {
        g_Logger.Error("  Erreur création effet rampe ", name, ": ", strerror(errno));
        return false;
    }
    
    m_Effects[name] = effect;
    g_Logger.Info("  Effet rampe créé: ", name, " (", startForce, " -> ", endForce, ", ID: ", effect.id, ")");
    return true;
}

/**
 * Crée un effet de condition (ressort, amortissement, etc.).
 */
bool ForceEffectSimulator::CreateConditionEffect(const std::string& name, uint16_t type,
                                                int16_t coefficient, uint16_t saturation)
{
    struct ff_effect effect;
    memset(&effect, 0, sizeof(effect));
    
    effect.type = type;
    effect.id = -1;
    effect.direction = 0x4000;
    
    // Configuration pour un seul axe (le volant)
    effect.u.condition[0].right_saturation = saturation;
    effect.u.condition[0].left_saturation = saturation;
    effect.u.condition[0].right_coeff = coefficient;
    effect.u.condition[0].left_coeff = coefficient;
    effect.u.condition[0].deadband = 500;
    effect.u.condition[0].center = 0;
    
    effect.trigger.button = 0;
    effect.trigger.interval = 0;
    effect.replay.length = INFINITE_DURATION;
    effect.replay.delay = 0;
    
    if (ioctl(m_DeviceFd, EVIOCSFF, &effect) < 0)
    {
        g_Logger.Error("  Erreur création effet condition ", name, ": ", strerror(errno));
        return false;
    }
    
    m_Effects[name] = effect;
    g_Logger.Info("  Effet condition créé: ", name, " (Coeff: ", coefficient, ", DeadBand: 500, ID: ", effect.id, ")");
    return true;
}

/**
 * Boucle principale du simulateur.
 */
void ForceEffectSimulator::Run()
{
    if (m_Effects.empty())
    {
        std::cerr << "Aucun effet disponible" << std::endl;
        return;
    }
    
    m_bRunning = true;
    
    // Configuration du terminal en mode raw
    m_TerminalMode.SetRaw();
    
    // Démarrage du thread de mise à jour
    m_UpdateThread = std::thread(&ForceEffectSimulator::UpdateLoop, this);
    
    DisplayHelp();
    DisplayStatus();
    
    // Boucle principale d'interface utilisateur
    while (m_bRunning)
    {
        if (kbhit())
        {
            char key;
            read(STDIN_FILENO, &key, 1);
            
            switch (key)
            {
            case 27: // ESC
                if (m_bShowingHelp)
                {
                    m_bShowingHelp = false;
                }
                else
                {
                    m_bRunning = false;
                }
                break;
                
            case ' ': // SPACE
                if (!m_bShowingHelp)
                {
                    if (m_bEffectPlaying)
                        StopCurrentEffect();
                    else
                        PlayCurrentEffect();
                }
                break;
                
            case 's':
            case 'S':
                if (!m_bShowingHelp)
                {
                    StopAllEffects();
                }
                break;
                
            case 'n':
            case 'N':
                if (!m_bShowingHelp)
                {
                    NextEffect();
                }
                break;
                
            case 'p':
            case 'P':
                if (!m_bShowingHelp)
                {
                    PreviousEffect();
                }
                break;
                
            case '+':
            case '=':
                if (!m_bShowingHelp)
                {
                    AdjustIntensity(2000);
                }
                break;
                
            case '-':
            case '_':
                if (!m_bShowingHelp)
                {
                    AdjustIntensity(-2000);
                }
                break;
                
            case 'h':
            case 'H':
                m_bShowingHelp = !m_bShowingHelp;
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
 * Thread de mise à jour : actualise l'état du périphérique.
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
 * Met à jour l'état du périphérique (lecture des axes/boutons).
 */
void ForceEffectSimulator::UpdateDeviceState()
{
    if (m_JoystickFd < 0) return;
    
    struct input_event ev;
    while (read(m_JoystickFd, &ev, sizeof(ev)) == sizeof(ev))
    {
        if (ev.type == EV_ABS)
        {
            switch (ev.code)
            {
            case ABS_X:
                m_SteeringValue = ev.value;
                break;
            case ABS_Y:
                m_Pedal1Value = ev.value;
                break;
            case ABS_Z:
                m_Pedal2Value = ev.value;
                break;
            }
        }
        else if (ev.type == EV_KEY)
        {
            if (ev.code >= BTN_JOYSTICK && ev.code < BTN_JOYSTICK + 32)
            {
                int button = ev.code - BTN_JOYSTICK;
                if (ev.value)
                    m_ButtonState |= (1 << button);
                else
                    m_ButtonState &= ~(1 << button);
            }
        }
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
        struct input_event play;
        play.type = EV_FF;
        play.code = it->second.id;
        play.value = 1; // Start effect
        
        if (write(m_DeviceFd, &play, sizeof(play)) != sizeof(play))
        {
            g_Logger.Error("Erreur lors de la lecture de l'effet: ", strerror(errno));
        }
        else
        {
            m_bEffectPlaying = true;
            g_Logger.Success(">>> EFFET JOUÉ: ", effectName, " <<<");
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
        struct input_event stop;
        stop.type = EV_FF;
        stop.code = it->second.id;
        stop.value = 0; // Stop effect
        
        write(m_DeviceFd, &stop, sizeof(stop));
        m_bEffectPlaying = false;
        g_Logger.Info(">>> EFFET ARRÊTÉ <<<");
    }
}

void ForceEffectSimulator::StopAllEffects()
{
    for (auto& pair : m_Effects)
    {
        struct input_event stop;
        stop.type = EV_FF;
        stop.code = pair.second.id;
        stop.value = 0;
        write(m_DeviceFd, &stop, sizeof(stop));
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
    m_ForceIntensity = std::max(static_cast<int16_t>(-MAX_FORCE), 
                                std::min(static_cast<int16_t>(MAX_FORCE), m_ForceIntensity));
    
    // Note: Modifier l'intensité d'un effet en cours nécessiterait de
    // le recréer avec les nouveaux paramètres sous Linux
}

void ForceEffectSimulator::AdjustDirection(int delta)
{
    m_EffectDirection += delta;
    m_EffectDirection = std::max(static_cast<int16_t>(-MAX_FORCE), 
                                 std::min(static_cast<int16_t>(MAX_FORCE), m_EffectDirection));
}

void ForceEffectSimulator::AdjustDuration(int delta)
{
    if (m_EffectDuration == INFINITE_DURATION)
    {
        m_EffectDuration = 2000;
    }
    else
    {
        m_EffectDuration += delta;
        m_EffectDuration = std::max(100u, std::min(10000u, m_EffectDuration));
    }
}

void ForceEffectSimulator::DisplayStatus()
{
    std::cout << "\033[2J\033[1;1H"; // Clear screen ANSI
    
    std::cout << "=== SIMULATEUR FORCE FEEDBACK SIDEWINDER (Linux) ===" << std::endl;
    std::cout << "=====================================================" << std::endl;
    
    // État du périphérique
    std::cout << "Device: " << (m_bDeviceOpen ? "CONNECTÉ" : "DÉCONNECTÉ") << std::endl;
    std::cout << "Path: " << m_DevicePath << std::endl;
    
    if (m_bDeviceOpen)
    {
        std::cout << "Position volant: " << m_SteeringValue << std::endl;
        std::cout << "Pédales: Acc=" << m_Pedal1Value << " Frein=" << m_Pedal2Value << std::endl;
        
        // Affichage des boutons pressés
        std::cout << "Boutons: ";
        bool hasButtons = false;
        for (int i = 0; i < 32; i++)
        {
            if (m_ButtonState & (1 << i))
            {
                std::cout << i << " ";
                hasButtons = true;
            }
        }
        if (!hasButtons) std::cout << "Aucun";
        std::cout << std::endl;
    }
    
    std::cout << "=====================================================" << std::endl;
    
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
    
    std::cout << "=====================================================" << std::endl;
    
    // Liste des effets disponibles
    std::cout << "Effets disponibles:" << std::endl;
    for (size_t i = 0; i < m_EffectNames.size(); i++)
    {
        std::cout << "  " << (i == m_CurrentEffectIndex ? "►" : " ") << " " << m_EffectNames[i] << std::endl;
    }
    
    std::cout << "=====================================================" << std::endl;
}

void ForceEffectSimulator::DisplayHelp()
{
    std::cout << "\033[2J\033[1;1H"; // Clear screen ANSI
    
    std::cout << "===================================================" << std::endl;
    std::cout << "         AIDE - SIMULATEUR FFB (Linux)            " << std::endl;
    std::cout << "===================================================" << std::endl;
    std::cout << std::endl;
    
    std::cout << "CONTRÔLES PRINCIPAUX:" << std::endl;
    std::cout << "  ESPACE      Jouer/Arrêter l'effet courant" << std::endl;
    std::cout << "  N           Effet suivant" << std::endl;
    std::cout << "  P           Effet précédent" << std::endl;
    std::cout << "  S           Arrêter tous les effets" << std::endl;
    std::cout << std::endl;
    
    std::cout << "AJUSTEMENTS:" << std::endl;
    std::cout << "  +  =        Augmenter l'intensité (+2000)" << std::endl;
    std::cout << "  -  _        Diminuer l'intensité (-2000)" << std::endl;
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
    
    std::cout << "PERMISSIONS:" << std::endl;
    std::cout << "  Si erreur d'accès, exécutez:" << std::endl;
    std::cout << "    sudo chmod 666 " << m_DevicePath << std::endl;
    std::cout << "  Ou ajoutez votre utilisateur au groupe 'input'" << std::endl;
    std::cout << std::endl;
    
    std::cout << "===================================================" << std::endl;
    std::cout << "   Appuyez sur H ou ESC pour revenir au menu      " << std::endl;
    std::cout << "===================================================" << std::endl;
}

std::string ForceEffectSimulator::FormatForce(int16_t force)
{
    double percentage = (force * 100.0) / MAX_FORCE;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d (%.1f%%)", (int)force, percentage);
    return std::string(buffer);
}

std::string ForceEffectSimulator::FormatDirection(int16_t direction)
{
    if (direction == 0) return "Centre";
    else if (direction > 0) return "Droite (" + std::to_string(direction) + ")";
    else return "Gauche (" + std::to_string(direction) + ")";
}

std::string ForceEffectSimulator::FormatDuration(uint32_t duration)
{
    if (duration == INFINITE_DURATION) return "Infinie";
    else return std::to_string(duration) + "ms";
}

void ForceEffectSimulator::CleanupEffects()
{
    for (auto& pair : m_Effects)
    {
        // Suppression de l'effet du kernel
        if (ioctl(m_DeviceFd, EVIOCRMFF, pair.second.id) < 0)
        {
            g_Logger.Warning("Erreur suppression effet ", pair.first);
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
    
    if (m_JoystickFd >= 0)
    {
        close(m_JoystickFd);
        m_JoystickFd = -1;
    }
    
    if (m_DeviceFd >= 0)
    {
        // Réactivation de l'autocenter avant de quitter
        struct input_event ie;
        ie.type = EV_FF;
        ie.code = FF_AUTOCENTER;
        ie.value = 0xFFFF;
        write(m_DeviceFd, &ie, sizeof(ie));
        
        close(m_DeviceFd);
        m_DeviceFd = -1;
    }
}

//==============================================================================
// FONCTION MAIN
//==============================================================================

int main()
{
    // Génération du nom de fichier log avec timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_r(&time_t_now, &timeinfo);
    
    char logFilename[256];
    strftime(logFilename, sizeof(logFilename), "FFB_Simulator_%Y%m%d_%H%M%S.log", &timeinfo);
    
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
    
    g_Logger.Info("Démarrage du simulateur Force Feedback Linux...");
    
    ForceEffectSimulator simulator;
    
    if (!simulator.Initialize())
    {
        g_Logger.Error("Échec de l'initialisation!");
        std::cout << "\nVérifiez que:" << std::endl;
        std::cout << "  1. Le volant Sidewinder est connecté en USB" << std::endl;
        std::cout << "  2. Les pilotes sont chargés (lsusb pour vérifier)" << std::endl;
        std::cout << "  3. Vous avez les permissions sur /dev/input/eventX" << std::endl;
        std::cout << "\nAppuyez sur Entrée pour continuer..." << std::endl;
        std::cin.get();
        return -1;
    }
    
    simulator.Run();
    
    g_Logger.Info("Arrêt du simulateur...");
    simulator.Shutdown();
    
    g_Logger.Info("Fichier log sauvegardé: ", g_Logger.GetFilename());
    g_Logger.Close();
    
    return 0;
}

//==============================================================================
// MAKEFILE SUGGÉRÉ
//==============================================================================

/*
# Makefile pour Linux

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
TARGET = FFB_Simulator

$(TARGET): FFB_Simulator_Linux.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) FFB_Simulator_Linux.cpp

clean:
	rm -f $(TARGET) *.log

install:
	install -m 755 $(TARGET) /usr/local/bin/

# Pour compiler simplement:
# g++ -std=c++17 -pthread -o FFB_Simulator FFB_Simulator_Linux.cpp

Configuration requise:
- Kernel Linux avec support evdev et force feedback
- Headers Linux (linux/input.h, linux/uinput.h)
- Microsoft Sidewinder Force Feedback Wheel connecté
- Permissions sur /dev/input/eventX ou membre du groupe 'input'

Pour donner les permissions:
  sudo chmod 666 /dev/input/eventX
  OU
  sudo usermod -aG input $USER
  (puis relogin)

Pour vérifier le device:
  ls -l /dev/input/by-id/*Sidewinder*
  evtest /dev/input/eventX
*/
