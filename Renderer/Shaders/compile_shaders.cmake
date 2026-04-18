# Shader compilation rules for DXC (DirectX Shader Compiler).
# Compiles HLSL shaders to both DXBC (D3D11) and SPIR-V (Vulkan).
#
# Usage: include from root CMakeLists.txt after finding DXC.
#   find_program(DXC_EXECUTABLE dxc HINTS "${VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin")
#   include(Renderer/Shaders/compile_shaders.cmake)

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/Shaders/VulkanHLSL")
set(SHADER_OUT "${CMAKE_BINARY_DIR}/Shaders")

file(MAKE_DIRECTORY "${SHADER_OUT}/dxbc")
file(MAKE_DIRECTORY "${SHADER_OUT}/spirv")

# Each entry: HLSL_FILE VS_ENTRY PS_ENTRY [VS_ENTRY2 PS_ENTRY2 ...]
# Multiple VS/PS pairs from the same HLSL file are compiled separately.
set(SHADER_ENTRIES
    # Shader3D — main 3D shader with many entry points
    "Shader3D.hlsl|VSMain|PSMain"
    "Shader3D.hlsl|VSMain|PSMainUnlit"
    "Shader3D.hlsl|VSMain|PSLaserGlow"
    "Shader3D.hlsl|VSMain|PSMainAlphaTest"
    "Shader3D.hlsl|VSMain|PSMeshDecal"
    "Shader3D.hlsl|VSMain|PSMainAlphaTestEdge"
    "Shader3D.hlsl|VSMain|PSMainSmudge"
    "Shader3D.hlsl|VSMain|PSGhost"
    "Shader3D.hlsl|VSMainTwoTex|PSMainTerrainMaskBase"
    "Shader3D.hlsl|VSMainWater|PSMainWaterBump"
    "Shader3D.hlsl|VSMainSkybox|PSMainSkybox"
    # 2D shader
    "Shader2D.hlsl|VSMain|PSMainColor"
    "Shader2D.hlsl|VSMain|PSMainTextured"
    "Shader2D.hlsl|VSMain|PSMainGrayscale"
    # Snow
    "ShaderSnow.hlsl|VSSnow|PSSnow"
    # Terrain decals
    "ShaderDecal.hlsl|VSDecal|PSDecal"
    # Post-processing
    "ShaderPost.hlsl|VSPost|PSBloomExtract"
    "ShaderPost.hlsl|VSPost|PSBlur"
    "ShaderPost.hlsl|VSPost|PSComposite"
    # FSR
    "ShaderFSR.hlsl|VSPost|PSEASU"
    "ShaderFSR.hlsl|VSPost|PSRCAS"
    # Particle FX
    "ShaderParticleFX.hlsl|VSPost|PSParticleExtract"
    "ShaderParticleFX.hlsl|VSPost|PSHeatDistort"
    "ShaderParticleFX.hlsl|VSPost|PSGlowComposite"
    # Shockwave
    "ShaderShockwave.hlsl|VSPost|PSShockwave"
    # God rays
    "ShaderGodRays.hlsl|VSPost|PSGodRayExtract"
    "ShaderGodRays.hlsl|VSPost|PSGodRayBlur"
    "ShaderGodRays.hlsl|VSPost|PSGodRayComposite"
    # Cinematic
    "ShaderCinematic.hlsl|VSPost|PSCinematic"
    # Film grain
    "ShaderFilmGrain.hlsl|VSPost|PSFilmGrain"
    # Sharpen
    "ShaderSharpen.hlsl|VSPost|PSSharpen"
    # Tilt shift
    "ShaderTiltShift.hlsl|VSPost|PSTiltShift"
    # Shadow depth + silhouette bake
    "ShaderShadowDepth.hlsl|VSShadowDepth|PSShadowDepth"
    "ShaderShadowDepth.hlsl|VSShadowDepth|PSShadowSilhouette"
    # Volumetric
    "ShaderVolumetric.hlsl|VSPost|PSVolumetric"
    # Lens flare
    "ShaderLensFlare.hlsl|VSPost|PSLensFlare"
    # Debug line overlays (Render::Debug)
    "ShaderDebug.hlsl|VSMain|PSMain"
    # GPU particles (compute)
    "GPUParticleUpdate.hlsl|CSUpdate|_"
    # GPU particles (render)
    "GPUParticleRender.hlsl|VSParticle|PSParticle"
)

if(DXC_EXECUTABLE)
    set(COMPILED_SHADERS "")

    foreach(ENTRY ${SHADER_ENTRIES})
        string(REPLACE "|" ";" PARTS "${ENTRY}")
        list(GET PARTS 0 HLSL_FILE)
        list(GET PARTS 1 VS_ENTRY)
        list(GET PARTS 2 PS_ENTRY)

        set(HLSL_PATH "${SHADER_DIR}/${HLSL_FILE}")
        get_filename_component(BASE_NAME "${HLSL_FILE}" NAME_WE)

        # --- Vertex shader (or compute shader) ---
        if(VS_ENTRY MATCHES "^CS")
            # Compute shader
            set(CS_SPIRV "${SHADER_OUT}/spirv/${BASE_NAME}_${VS_ENTRY}.spv")
            add_custom_command(
                OUTPUT "${CS_SPIRV}"
                COMMAND "${DXC_EXECUTABLE}" -T cs_6_0 -E ${VS_ENTRY} -spirv
                    -fvk-b-shift 0 0 -fvk-t-shift 10 0 -fvk-s-shift 20 0 -fvk-u-shift 30 0
                    -fspv-target-env=vulkan1.0
                    "${HLSL_PATH}" -Fo "${CS_SPIRV}"
                DEPENDS "${HLSL_PATH}"
                COMMENT "DXC: ${HLSL_FILE} [${VS_ENTRY}] -> SPIR-V"
            )
            list(APPEND COMPILED_SHADERS "${CS_SPIRV}")
        else()
            # Vertex shader -> SPIR-V
            set(VS_SPIRV "${SHADER_OUT}/spirv/${BASE_NAME}_${VS_ENTRY}.spv")
            add_custom_command(
                OUTPUT "${VS_SPIRV}"
                COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E ${VS_ENTRY} -spirv
                    -fvk-b-shift 0 0 -fvk-t-shift 10 0 -fvk-s-shift 20 0 -fvk-u-shift 30 0
                    -fspv-target-env=vulkan1.0
                    "${HLSL_PATH}" -Fo "${VS_SPIRV}"
                DEPENDS "${HLSL_PATH}"
                COMMENT "DXC: ${HLSL_FILE} [${VS_ENTRY}] -> SPIR-V"
            )
            list(APPEND COMPILED_SHADERS "${VS_SPIRV}")

            # Pixel shader -> SPIR-V (skip if marked "_")
            if(NOT PS_ENTRY STREQUAL "_")
                set(PS_SPIRV "${SHADER_OUT}/spirv/${BASE_NAME}_${PS_ENTRY}.spv")
                add_custom_command(
                    OUTPUT "${PS_SPIRV}"
                    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E ${PS_ENTRY} -spirv
                        -fvk-b-shift 0 0 -fvk-t-shift 10 0 -fvk-s-shift 20 0 -fvk-u-shift 30 0
                        -fspv-target-env=vulkan1.0
                        "${HLSL_PATH}" -Fo "${PS_SPIRV}"
                    DEPENDS "${HLSL_PATH}"
                    COMMENT "DXC: ${HLSL_FILE} [${PS_ENTRY}] -> SPIR-V"
                )
                list(APPEND COMPILED_SHADERS "${PS_SPIRV}")
            endif()
        endif()
    endforeach()

    # Custom target that depends on all compiled shaders
    add_custom_target(compile_shaders DEPENDS ${COMPILED_SHADERS})

    message(STATUS "Shader compilation: ${DXC_EXECUTABLE} -> ${SHADER_OUT}/spirv/")
else()
    message(WARNING "DXC not found — shaders will not be compiled to SPIR-V. Vulkan rendering will not work.")
endif()
