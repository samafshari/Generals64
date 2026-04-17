# SDL3 external dependency.
#
# On Windows:  Place SDL3 dev files here (include/SDL3/*.h, lib/SDL3.lib, bin/SDL3.dll)
#              OR set SDL3_DIR to point to an installed SDL3 CMake config.
# On macOS:    brew install sdl3, or use the SDL3 framework.
# On Linux:    apt install libsdl3-dev, or build from source.

# Try system-installed SDL3 first
find_package(SDL3 QUIET)

if(SDL3_FOUND)
    # System SDL3 found — create an alias target
    add_library(sdl3lib INTERFACE)
    target_link_libraries(sdl3lib INTERFACE SDL3::SDL3)
    target_compile_definitions(sdl3lib INTERFACE -DUSE_SDL)
else()
    # Fall back to vendored SDL3 in this directory
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/SDL3/SDL.h")
        add_library(sdl3lib INTERFACE)
        target_include_directories(sdl3lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_definitions(sdl3lib INTERFACE -DUSE_SDL)

        if(WIN32)
            target_link_libraries(sdl3lib INTERFACE
                ${CMAKE_CURRENT_SOURCE_DIR}/lib/SDL3.lib
            )
            # DLL will be copied by the main target's post-build step
            set(SDL3_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/bin/SDL3.dll" CACHE INTERNAL "")
        elseif(APPLE)
            find_library(SDL3_FRAMEWORK SDL3 PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib)
            if(SDL3_FRAMEWORK)
                target_link_libraries(sdl3lib INTERFACE ${SDL3_FRAMEWORK})
            endif()
        endif()
    else()
        message(WARNING "SDL3 not found. Install SDL3 or place dev files in extern/sdl3/")
        # Create a dummy target so the build doesn't fail immediately
        add_library(sdl3lib INTERFACE)
    endif()
endif()
