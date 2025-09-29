cmake_minimum_required(VERSION 3.20)
project(FFB_Simulator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Recherche automatique des bibliothèques DirectX
find_library(DINPUT8_LIB dinput8)
find_library(DXGUID_LIB dxguid)

if(NOT DINPUT8_LIB OR NOT DXGUID_LIB)
    message(FATAL_ERROR "DirectInput libraries not found. Install DirectX SDK or Windows SDK.")
endif()

add_executable(FFB_Simulator FFB_Simulator.cpp)

target_link_libraries(FFB_Simulator 
    ${DINPUT8_LIB}
    ${DXGUID_LIB}
    user32
    kernel32
)

# Configuration pour Visual Studio
if(MSVC)
    target_compile_options(FFB_Simulator PRIVATE /W4 /EHsc)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT FFB_Simulator)
endif()
