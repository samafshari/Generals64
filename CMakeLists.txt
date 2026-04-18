cmake_minimum_required(VERSION 3.20)
project(GeneralsZeroHour LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Platform and backend options ---
option(USE_SDL "Use SDL3 for windowing and input" ON)

# Public / ship build. Mirrors the ReleasePublic MSBuild config: strips
# diagnostic bloat that spams logs or burns CPU (Inspector log window,
# SQLite telemetry writer, DEBUG_LOG). Controlled by a CMake option so
# single-config generators (Ninja, Make) can opt in via -DPUBLIC_BUILD=ON;
# the VS generator users should just pick the ReleasePublic configuration.
option(PUBLIC_BUILD "Strip diagnostic/telemetry bloat (matches ReleasePublic)" OFF)

if(WIN32)
    option(USE_D3D11 "Build with Direct3D 11 backend" ON)
    option(USE_VULKAN "Build with Vulkan backend" OFF)
elseif(APPLE)
    set(USE_D3D11 OFF CACHE BOOL "" FORCE)
    option(USE_VULKAN "Build with Vulkan backend (MoltenVK)" ON)
else()
    set(USE_D3D11 OFF CACHE BOOL "" FORCE)
    option(USE_VULKAN "Build with Vulkan backend" ON)
endif()

# --- External dependencies ---
add_subdirectory(extern/sdl3)

if(USE_D3D11)
    add_subdirectory(extern/d3d11)
endif()

add_subdirectory(extern/ffmpeg)

if(USE_VULKAN)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        # Fall back to vendored Vulkan headers + system loader
        message(STATUS "Vulkan SDK not found, using vendored headers from extern/vulkan/")
        add_library(Vulkan::Headers INTERFACE IMPORTED)
        set_target_properties(Vulkan::Headers PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/extern/vulkan/include")
        # On macOS with MoltenVK, the loader is provided by the MoltenVK package
        # On Linux, vulkan-1 is provided by the system
        add_library(Vulkan::Vulkan INTERFACE IMPORTED)
        set_target_properties(Vulkan::Vulkan PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/extern/vulkan/include")
        if(APPLE)
            # MoltenVK provides the Vulkan ICD; link the loader
            find_library(MOLTENVK_LIB MoltenVK)
            if(MOLTENVK_LIB)
                set_target_properties(Vulkan::Vulkan PROPERTIES INTERFACE_LINK_LIBRARIES "${MOLTENVK_LIB}")
            endif()
        elseif(WIN32)
            # On Windows, link vulkan-1.lib from the SDK or require it
            find_library(VULKAN_LIB vulkan-1)
            if(VULKAN_LIB)
                set_target_properties(Vulkan::Vulkan PROPERTIES INTERFACE_LINK_LIBRARIES "${VULKAN_LIB}")
            endif()
        else()
            # Linux: dynamic load via dlopen at runtime
            set_target_properties(Vulkan::Vulkan PROPERTIES INTERFACE_LINK_LIBRARIES "dl")
        endif()
    endif()
endif()

# --- Shader compilation ---

# Step 1: generate_inc.py — every Renderer/Shaders/VulkanHLSL/*.hlsl becomes
# a sibling *.hlsl.inc that ShaderSource.h / GPUParticleShaders.h consume as
# raw-string-literal initializers for the D3D11 runtime-compile path.
# Uses a custom_target with ALL so it runs every configure; the script itself
# skips unchanged files.
find_program(PYTHON_EXECUTABLE NAMES python3 python)
if(PYTHON_EXECUTABLE)
    add_custom_target(generate_shader_includes ALL
        COMMAND "${PYTHON_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/Shaders/scripts/generate_inc.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/Shaders/VulkanHLSL"
        COMMENT "Generating shader .inc files from .hlsl sources"
        VERBATIM
    )
else()
    message(WARNING "Python not found — shader .inc files will not be regenerated automatically. Edit .hlsl files and run Renderer/Shaders/scripts/generate_inc.py manually.")
endif()

# Step 2: compile_shaders.cmake — dxc invocations that produce the Vulkan
# SPIR-V blobs. Separate from step 1 because DX11 builds also need step 1
# to keep ShaderSource.h's includes fresh.
find_program(DXC_EXECUTABLE dxc HINTS "${VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin")
if(DXC_EXECUTABLE)
    include(Renderer/Shaders/compile_shaders.cmake)
endif()

# --- Source files (extracted from vcxproj) ---
include(cmake/sources.cmake)

# On non-Windows, filter out Win32-only sources
if(NOT WIN32)
    list(FILTER GAME_SOURCES EXCLUDE REGEX ".*Win32Device.*")
    list(FILTER GAME_SOURCES EXCLUDE REGEX ".*MilesAudioDevice.*")
    list(FILTER GAME_SOURCES EXCLUDE REGEX ".*VideoEnhancer.*")
    list(FILTER GAME_SOURCES EXCLUDE REGEX ".*WOLBrowser.*")
    list(FILTER GAME_SOURCES EXCLUDE REGEX ".*AudioVideoStubs.*")
    list(FILTER GAME_SOURCES EXCLUDE REGEX ".*WWAudio.*")
    list(FILTER GAME_SOURCES EXCLUDE REGEX ".*wwmemlog.*")
endif()

# Inspector panels are ImGui-dependent and assume the D3D11 ImGui backend.
# In a pure-Vulkan build (USE_D3D11=OFF), Inspector.cpp's #else branch
# compiles to no-op stubs, so we drop the panel sources too (they'd
# collide with the stubs at link time). Mirrors the ExcludedFromBuild
# conditions on these files in the DebugVK/ReleaseVK vcxproj configs.
if(NOT USE_D3D11)
    list(FILTER GAME_SOURCES EXCLUDE REGEX "Inspector/Panels\\.cpp$")
    list(FILTER GAME_SOURCES EXCLUDE REGEX "Inspector/HudPanels\\.cpp$")
    list(FILTER GAME_SOURCES EXCLUDE REGEX "Inspector/AIPanels\\.cpp$")
    list(FILTER GAME_SOURCES EXCLUDE REGEX "Inspector/ScriptPanel\\.cpp$")
    list(FILTER GAME_SOURCES EXCLUDE REGEX "Inspector/DestructionTimeline\\.cpp$")
endif()

# --- Create executable ---
add_executable(generalszh ${GAME_SOURCES})

# Precompiled header (C++ only — C files like zlib don't use it)
target_precompile_headers(generalszh PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:pch.h>")

if(TARGET compile_shaders)
    add_dependencies(generalszh compile_shaders)
endif()
if(TARGET generate_shader_includes)
    add_dependencies(generalszh generate_shader_includes)
endif()

# --- Include directories ---
target_include_directories(generalszh PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    extern/imgui
    extern/imgui/backends
    GeneralsMD/Code/Main
    GeneralsMD/Code/GameEngine/Include
    GeneralsMD/Code/GameEngine/Include/Precompiled
    GeneralsMD/Code/GameEngineDevice/Include
    GeneralsMD/Code/GameEngineDevice/Include/Stubs
    GeneralsMD/Code/GameEngineDevice/Include/Stubs/mss
    GeneralsMD/Code/Libraries/Source/WWVegas
    GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2
    Core/GameEngine/Include
    Core/GameEngineDevice/Include
    Core/Libraries/Include
    Core/Libraries/Source/debug
    Core/Libraries/Source/profile
    Core/Libraries/Source/WWVegas
    Core/Libraries/Source/WWVegas/WW3D2
    Core/Libraries/Source/WWVegas/WWAudio
    Core/Libraries/Source/WWVegas/WWDebug
    Core/Libraries/Source/WWVegas/WWLib
    Core/Libraries/Source/WWVegas/WWMath
    Core/Libraries/Source/WWVegas/WWSaveLoad
    Core/Libraries/Source/Compression
    Core/Libraries/Source/EABrowserDispatch/stub
    Core/Libraries/Source/EABrowserEngine/stub
    Core/Main
    Platform
    Renderer
    Renderer/Core
    extern/d3d11
    extern/vulkan/include
    extern/json
    extern/zlib
    extern/sqlite3
    extern/bink
    extern/lzhl_wrapper
    extern/lzhl_wrapper/CompLibHeader
    extern/ffmpeg/include
    resources/gitinfo
)

# ATL headers (required by PreRTS.h on Windows)
if(WIN32)
    # Search all installed MSVC toolset versions for ATL
    file(GLOB MSVC_VERSIONS "$ENV{VCToolsInstallDir}/atlmfc/include"
                            "C:/Program Files/Microsoft Visual Studio/*/Community/VC/Tools/MSVC/*/atlmfc/include"
                            "C:/Program Files/Microsoft Visual Studio/*/Enterprise/VC/Tools/MSVC/*/atlmfc/include"
                            "C:/Program Files/Microsoft Visual Studio/*/Professional/VC/Tools/MSVC/*/atlmfc/include"
                            "C:/Program Files (x86)/Microsoft Visual Studio/*/Community/VC/Tools/MSVC/*/atlmfc/include"
    )
    if(MSVC_VERSIONS)
        list(GET MSVC_VERSIONS -1 ATL_INCLUDE)
        target_include_directories(generalszh PRIVATE "${ATL_INCLUDE}")
        # Also add ATL lib directory
        get_filename_component(ATL_BASE "${ATL_INCLUDE}" DIRECTORY)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            target_link_directories(generalszh PRIVATE "${ATL_BASE}/lib/x64")
        else()
            target_link_directories(generalszh PRIVATE "${ATL_BASE}/lib/x86")
        endif()
        message(STATUS "ATL: ${ATL_BASE}")
    endif()
endif()

# --- Compile definitions ---
target_compile_definitions(generalszh PRIVATE
    RTS_RELEASE
    NDEBUG
    DEBUG_LOGGING=1
    DISABLE_DEBUG_CRASHING=1
    DISABLE_DEBUG_STACKTRACE=1
    DISABLE_MEMORYPOOL_CHECKPOINTING=1
    DISABLE_MEMORYPOOL_STACKTRACE=1
    RTS_ENABLE_CRASHDUMP=1
    IG_DEBUG_STACKTRACE
    RTS_ZEROHOUR=1
    Z_PREFIX
    RTS_HAS_FFMPEG
    _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
    _CRT_SECURE_NO_WARNINGS
)

if(USE_SDL)
    target_compile_definitions(generalszh PRIVATE USE_SDL)
endif()

if(PUBLIC_BUILD)
    target_compile_definitions(generalszh PRIVATE
        RTS_PUBLIC=1
        GENERALS_TELEMETRY=0
        DISABLE_DEBUG_LOGGING=1
    )
endif()

if(USE_D3D11)
    target_compile_definitions(generalszh PRIVATE BUILD_WITH_D3D11)
endif()

if(USE_VULKAN)
    target_compile_definitions(generalszh PRIVATE BUILD_WITH_VULKAN)
endif()

# --- Link libraries ---
target_link_libraries(generalszh PRIVATE sdl3lib)

if(USE_D3D11)
    target_link_libraries(generalszh PRIVATE d3d11lib)
endif()

if(USE_VULKAN)
    target_link_libraries(generalszh PRIVATE Vulkan::Vulkan)
endif()

target_link_libraries(generalszh PRIVATE ffmpeglibs)

if(WIN32)
    target_link_libraries(generalszh PRIVATE
        comctl32 imm32 vfw32 winmm version
        xaudio2 comsuppw ws2_32
        kernel32 user32 gdi32 shell32
        ole32 oleaut32 uuid comdlg32 advapi32
    )
endif()

if(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    target_link_libraries(generalszh PRIVATE
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
    )
endif()

# --- Output ---
set_target_properties(generalszh PROPERTIES
    OUTPUT_NAME "generalszh"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/GameData"
)

if(WIN32)
    set_target_properties(generalszh PROPERTIES WIN32_EXECUTABLE TRUE)

    # Copy runtime DLLs
    if(DEFINED SDL3_DLL_PATH AND EXISTS "${SDL3_DLL_PATH}")
        add_custom_command(TARGET generalszh POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL3_DLL_PATH}" $<TARGET_FILE_DIR:generalszh>
            COMMENT "Copying SDL3.dll...")
    endif()
endif()
