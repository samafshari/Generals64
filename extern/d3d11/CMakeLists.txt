add_library(d3d11lib INTERFACE)

# D3D11 + DXGI + shader compiler - all available in Windows SDK
target_link_libraries(d3d11lib INTERFACE
    d3d11
    dxgi
    d3dcompiler
    dxguid
)

target_compile_definitions(d3d11lib INTERFACE -DBUILD_WITH_D3D11)
target_include_directories(d3d11lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
