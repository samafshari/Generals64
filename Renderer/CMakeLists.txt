add_library(renderer INTERFACE)
target_sources(renderer INTERFACE
    Core/RHITypes.h
    Core/Device.cpp
    Core/Device.h
    Core/Buffer.cpp
    Core/Buffer.h
    Core/Shader.cpp
    Core/Shader.h
    Core/Texture.cpp
    Core/Texture.h
    Renderer.cpp
    Renderer.h
    Math/RenderMath.h
    # TerrainRenderer moved to GameEngineDevice (needs game headers)
    Shaders/ShaderSource.h
)

target_include_directories(renderer INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
)

target_link_libraries(renderer INTERFACE d3d11lib)

# Require C++17 for structured bindings and std::optional
target_compile_features(renderer INTERFACE cxx_std_17)
