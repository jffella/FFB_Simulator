cmake_minimum_required(VERSION 3.10)
project(FFB_Simulator VERSION 1.0.0 LANGUAGES CXX)

# Configuration C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options de compilation
option(BUILD_TESTS "Build test programs" OFF)
option(ENABLE_WARNINGS "Enable compiler warnings" ON)
option(ENABLE_OPTIMIZATION "Enable optimizations for Release build" ON)

# Détection de la plateforme
if(WIN32)
    set(PLATFORM "Windows")
    set(SOURCE_FILE "win/src/FFB_Simulator.cpp")
elseif(UNIX AND NOT APPLE)
    set(PLATFORM "Linux")
    set(SOURCE_FILE "linux/src/FFB_Simulator.cpp")
else()
    message(FATAL_ERROR "Plateforme non supportée. Seuls Windows et Linux sont supportés.")
endif()

message(STATUS "Plateforme détectée: ${PLATFORM}")
message(STATUS "Fichier source: ${SOURCE_FILE}")

# Vérification de l'existence du fichier source
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE}")
    message(FATAL_ERROR "Fichier source ${SOURCE_FILE} non trouvé!")
endif()

# Création de l'exécutable
add_executable(FFB_Simulator ${SOURCE_FILE})

# Configuration spécifique à la plateforme
if(WIN32)
    # Windows - DirectInput
    target_link_libraries(FFB_Simulator PRIVATE
        dinput8
        dxguid
        user32
        kernel32
    )
    
    # Définitions spécifiques Windows
    target_compile_definitions(FFB_Simulator PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        DIRECTINPUT_VERSION=0x0800
        _CRT_SECURE_NO_WARNINGS
    )
    
    # Chemins des bibliothèques Windows (peut nécessiter ajustement)
    if(MSVC)
        # Pour Visual Studio, les libs sont généralement trouvées automatiquement
        message(STATUS "Compilateur MSVC détecté")
    endif()
    
elseif(UNIX)
    # Linux - evdev + pthreads
    target_link_libraries(FFB_Simulator PRIVATE
        pthread
    )
    
    # Pas de bibliothèques supplémentaires nécessaires pour evdev
    # (headers kernel standard)
endif()

# Options de compilation communes
if(ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(FFB_Simulator PRIVATE
            /W4          # Niveau d'avertissements élevé
            /WX-         # Ne pas traiter les warnings comme des erreurs
        )
    else()
        target_compile_options(FFB_Simulator PRIVATE
            -Wall        # Tous les avertissements
            -Wextra      # Avertissements supplémentaires
            -Wpedantic   # Respect strict du standard
            -Wno-unused-parameter  # Ignore les paramètres non utilisés
        )
    endif()
endif()

# Optimisations pour Release
if(ENABLE_OPTIMIZATION)
    if(MSVC)
        target_compile_options(FFB_Simulator PRIVATE
            $<$<CONFIG:Release>:/O2>  # Optimisation maximale vitesse
            $<$<CONFIG:Release>:/MT>  # Runtime statique
        )
    else()
        target_compile_options(FFB_Simulator PRIVATE
            $<$<CONFIG:Release>:-O3>  # Optimisation maximale
            $<$<CONFIG:Release>:-march=native>  # Optimisation CPU
        )
    endif()
endif()

# Options Debug
if(MSVC)
    target_compile_options(FFB_Simulator PRIVATE
        $<$<CONFIG:Debug>:/Zi>   # Informations de debug
        $<$<CONFIG:Debug>:/Od>   # Pas d'optimisation
        $<$<CONFIG:Debug>:/MDd>  # Runtime debug
    )
else()
    target_compile_options(FFB_Simulator PRIVATE
        $<$<CONFIG:Debug>:-g>    # Informations de debug
        $<$<CONFIG:Debug>:-O0>   # Pas d'optimisation
    )
endif()

# Installation
install(TARGETS FFB_Simulator
    RUNTIME DESTINATION bin
)

# Fichiers de documentation
install(FILES
    README.md
    DESTINATION share/doc/FFB_Simulator
    OPTIONAL
)

# Affichage des informations de configuration
message(STATUS "==========================================")
message(STATUS "Configuration FFB Simulator")
message(STATUS "==========================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Plateforme: ${PLATFORM}")
message(STATUS "Compilateur: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Standard C++: ${CMAKE_CXX_STANDARD}")
message(STATUS "Type de build: ${CMAKE_BUILD_TYPE}")
message(STATUS "Warnings: ${ENABLE_WARNINGS}")
message(STATUS "Optimizations: ${ENABLE_OPTIMIZATION}")
message(STATUS "==========================================")

# Tests (optionnel)
if(BUILD_TESTS)
    enable_testing()
    
    # Exemple de test simple
    add_test(NAME version_test
        COMMAND FFB_Simulator --version
    )
endif()

# CPack pour créer des packages
set(CPACK_PACKAGE_NAME "FFB_Simulator")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Simulateur Force Feedback pour volant Microsoft Sidewinder")
set(CPACK_PACKAGE_VENDOR "FFB Project")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(UNIX)
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "FFB Project")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.27)")
endif()

include(CPack)
