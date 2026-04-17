# Pre-built FFmpeg shared libraries (LGPL, x64)
# Downloaded from https://github.com/BtbN/FFmpeg-Builds/releases
# Only avcodec, avformat, avutil, swscale, swresample are used.

add_library(ffmpeglibs INTERFACE)

target_include_directories(ffmpeglibs INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ffmpeglibs INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/avcodec.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/avformat.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/avutil.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/swscale.lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/swresample.lib
)

target_compile_definitions(ffmpeglibs INTERFACE
    RTS_HAS_FFMPEG
)

# Copy DLLs to the game output directory at build time
set(FFMPEG_DLL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(FFMPEG_DLLS
    "${FFMPEG_DLL_DIR}/avcodec-61.dll"
    "${FFMPEG_DLL_DIR}/avformat-61.dll"
    "${FFMPEG_DLL_DIR}/avutil-59.dll"
    "${FFMPEG_DLL_DIR}/swscale-8.dll"
    "${FFMPEG_DLL_DIR}/swresample-5.dll"
)
set(FFMPEG_DLLS "${FFMPEG_DLLS}" PARENT_SCOPE)
set(FFMPEG_DLL_DIR "${FFMPEG_DLL_DIR}" PARENT_SCOPE)
